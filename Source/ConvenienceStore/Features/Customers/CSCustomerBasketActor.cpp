#include "Features/Customers/CSCustomerBasketActor.h"

#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Items/CSProductPickupActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

ACSCustomerBasketActor::ACSCustomerBasketActor()
{
	// 추적 중에만 틱을 켭니다. 손님 CharacterMovement가 위치를 확정한 뒤 읽기 위해 PostPhysics에서 틱합니다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	PrimaryActorTick.TickInterval = 0.0f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	BasketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BasketMesh"));
	BasketMesh->SetupAttachment(SceneRoot);
	BasketMesh->SetMobility(EComponentMobility::Movable);
	BasketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SweepObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
	FloorTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
}

void ACSCustomerBasketActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서 액터를 배치하거나 설정을 변경하면 메시 반경을 갱신
	UpdateBasketRadiusFromMesh();
}

void ACSCustomerBasketActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	UpdateBasketRadiusFromMesh();
}

void ACSCustomerBasketActor::UpdateBasketRadiusFromMesh()
{
	// 장바구니 컴포넌트에 지정된 메시를 조회
	const UStaticMesh* Mesh = nullptr;
	if (IsValid(BasketMesh))
	{
		Mesh = BasketMesh->GetStaticMesh();
	}

	// 메시가 없으면 기본 반경을 적용하고 경고 출력
	if (Mesh == nullptr)
	{
		BasketRadius = FallbackBasketRadius;
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 장바구니 메시가 없어 반경을 기본값 %.1f로 대체합니다."),*GetName(),BasketRadius);
		return;
	}

	// 회전에 따라 크기가 달라지는 월드 축 기준 바운드 대신 로컬 바운드를 사용
	// 컴포넌트에 적용된 스케일을 별도로 반영
	const FBoxSphereBounds LocalBounds = Mesh->GetBounds();
	const FVector Scale = BasketMesh->GetComponentScale();

	// 메시 피벗에서 X축 양쪽 끝 중 먼 거리를 계산
	// 바운드 중심의 치우침과 스케일을 반영하며, 음수 스케일도 크기로 처리
	const float DistanceX = static_cast<float>((FMath::Abs(LocalBounds.Origin.X) + LocalBounds.BoxExtent.X) * FMath::Abs(Scale.X));

	// 메시 피벗에서 Y축 양쪽 끝 중 먼 거리를 계산
	const float DistanceY = static_cast<float>((FMath::Abs(LocalBounds.Origin.Y) + LocalBounds.BoxExtent.Y) * FMath::Abs(Scale.Y));

	// 두 축의 거리 중 큰 값을 판정 반경으로 사용
	// 메시 상대 위치가 0인 전제이며, 바운드 모서리까지 감싸는 반경은 아님
	const float RawRadius = FMath::Max(DistanceX, DistanceY);

	// 계산된 반경을 최소 1부터 설정한 최대 반경 사이로 제한
	BasketRadius = FMath::Clamp(RawRadius, 1.0f, MaxBasketRadius);

	// 상한에 의해 반경이 줄어들면 메시 크기와 NavMesh 설정 확인을 위한 경고 출력
	if (RawRadius > MaxBasketRadius)
	{
		UE_LOG(LogTemp, Warning,TEXT("%s: 메시 반경 %.1f가 상한 %.1f를 넘어 잘렸습니다. NavMesh AgentRadius(75)와 비교해 확인하세요."),*GetName(),RawRadius,MaxBasketRadius);
	}
}

bool ACSCustomerBasketActor::AddDisplayProduct(const FCSSelectProduct& SelectedProduct)
{
	if (GetWorld() == nullptr ||
		SelectedProduct.ProductClass == nullptr ||
		SelectedProduct.ProductRow.DataTable == nullptr ||
		SelectedProduct.ProductRow.RowName.IsNone())
	{
		return false;
	}
	UClass* ProductClass = SelectedProduct.ProductClass.Get();
	if (!IsValid(ProductClass) || !ProductClass->IsChildOf(ACSProductPickupActor::StaticClass()))
	{
		return false;
	}

	CollectDisplaySlots();
	if (!DisplaySlots.IsValidIndex(DisplayProducts.Num()) || !IsValid(DisplaySlots[DisplayProducts.Num()]))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 장바구니 표시 슬롯이 부족하여 %s 상품을 표시하지 못했습니다."), *GetName(), *SelectedProduct.ProductRow.RowName.ToString());
		return false;
	}

	UArrowComponent* DisplaySlot = DisplaySlots[DisplayProducts.Num()];
	const FTransform SpawnTransform = DisplaySlot->GetComponentTransform();
	ACSProductPickupActor* DisplayProduct =
		GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
			ProductClass,
			SpawnTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	// 생성이나 상품 초기화에 실패하면 생성된 액터를 정리하고 종료
	if (!IsValid(DisplayProduct) || !DisplayProduct->InitializeProduct(SelectedProduct.ProductRow))
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->Destroy();
		}
		return false;
	}

	DisplayProduct->SetFreshnessFrozenForDisplay(true);
	DisplayProduct->ApplyProductRuntimeState(SelectedProduct.ProductRuntimeState);
	DisplayProduct->FinishSpawning(SpawnTransform);
	DisplayProduct->PrepareForPickupPhysics();
	DisplayProduct->SetActorEnableCollision(false);
	// 장바구니 상품은 화면 표시 전용이므로 실제 매장 재고에 포함하지 않습니다.
	DisplayProduct->SetExcludedFromStockCount(true);
	DisplayProduct->AttachToComponent(DisplaySlot,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	DisplayProducts.Add(DisplayProduct);
	return true;
}

void ACSCustomerBasketActor::ClearDisplayProducts()
{
	for (ACSProductPickupActor* DisplayProduct : DisplayProducts)
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->Destroy();
		}
	}
	DisplayProducts.Reset();
}

void ACSCustomerBasketActor::SetBasketDisplayVisible(bool bShouldBeVisible)
{
	// 숨겨진 상태에서 다시 표시되는지 확인하기 위해 이전 상태를 보관
	const bool bWasVisible = bBasketDisplayVisible;
	bBasketDisplayVisible = bShouldBeVisible;

	if (IsValid(BasketMesh))
	{
		BasketMesh->SetHiddenInGame(!bShouldBeVisible);
	}

	for (ACSProductPickupActor* DisplayProduct : DisplayProducts)
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->SetActorHiddenInGame(!bShouldBeVisible);
		}
	}
	// 숨겨진 동안에는 추적 계산을 중단
	if (!bShouldBeVisible)
	{
		SetActorTickEnabled(false);
		return;
	}

	if (FollowTarget.IsValid())
	{
		// 다시 표시할 때 이전 위치에서 끌려오지 않도록 손님 주변에 재배치
		if (!bWasVisible)
		{
			TeleportToPark(TEXT("ShowAgain"));
		}
		SetActorTickEnabled(true);
	}
}

void ACSCustomerBasketActor::StartFollowing(ACSCustomerCharacter* InCustomer)
{
	if (!IsValid(InCustomer))
	{
		return;
	}

	// 다른 손님을 추적 중이면 기존 연결과 추적 상태를 정리
	if (FollowTarget.IsValid() && FollowTarget.Get() != InCustomer)
	{
		StopFollowing();
	}

	FollowTarget = InCustomer;

	// 손님 이동 컴포넌트의 Tick 이후에 장바구니가 갱신되도록 순서를 지정
	if (UCharacterMovementComponent* CustomerMovement = InCustomer->GetCharacterMovement())
	{
		AddTickPrerequisiteComponent(CustomerMovement);
	}

	// 대기 거리 계산에 사용할 메시 반경을 먼저 갱신
	UpdateBasketRadiusFromMesh();

	// 손님 주변에 배치하고 표시 중일 때만 추적 Tick을 활성화
	TeleportToPark(TEXT("StartFollowing"));
	SetActorTickEnabled(bBasketDisplayVisible);
}

void ACSCustomerBasketActor::StopFollowing()
{
	if (ACSCustomerCharacter* Customer = FollowTarget.Get())
	{
		if (UCharacterMovementComponent* CustomerMovement = Customer->GetCharacterMovement())
		{
			RemoveTickPrerequisiteComponent(CustomerMovement);
		}
	}

	FollowTarget.Reset();
	ResetFollowState();
	// 다음 추적 시작 시 바닥 높이를 새로 계산하도록 초기화
	bFloorZValid = false;
	SetActorTickEnabled(false);
}

void ACSCustomerBasketActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 추적 대상이 사라지면 추적을 종료
	ACSCustomerCharacter* Customer = FollowTarget.Get();
	if (!IsValid(Customer))
	{
		StopFollowing();
		return;
	}

	// 이동량 계산과 속도 환산에 사용할 시간이 너무 작으면 건너뜀
	if (DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 손님 발 위치를 기준으로 이동 경로를 기록
	const FVector Foot = GetCustomerFootLocation();
	RecordTrail(Foot);

	const FVector Current = GetActorLocation();

	// 손님과 너무 멀어지면 주변 대기 위치로 재배치
	if (FVector::DistSquared2D(Current, Foot) > FMath::Square(TeleportDistance))
	{
		TeleportToPark(TEXT("TooFar"));
		return;
	}

	// 끼임 상태가 제한 시간을 넘으면 주변 대기 위치로 재배치
	if (StuckTime > StuckTeleportTime)
	{
		TeleportToPark(TEXT("Stuck"));
		return;
	}

	// 손님의 수평 속도를 기준으로 이동 여부를 판정
	const bool bCustomerMoving = Customer->GetVelocity().Size2D() > MovingSpeedThreshold;

	// 이동 중이면 정지 시간을 초기화하고, 멈춰 있으면 정지 시간을 누적
	if (bCustomerMoving)
	{
		IdleTime = 0.0f;
	}
	else
	{
		IdleTime += DeltaSeconds;
	}

	FVector Target = Current;
	bool bHasTrailTarget = false;

	// 대기 모드 전환 전에는 손님이 지나온 경로에서 추적 위치를 계산
	if (IdleTime < IdleEnterDelay)
	{
		// 손님과 장바구니 반경에 추가 간격을 더해 추적 거리를 계산
		const float FollowDistance = GetCustomerBodyRadius() + BasketRadius + FollowGap;
		bHasTrailTarget = ComputeTrailTarget(Foot, FollowDistance, Target);
		if (bHasTrailTarget)
		{
			// 다음 대기 시 현재 손님 위치를 기준으로 자리를 다시 찾도록 초기화
			bParkTargetCached = false;
		}
	}

	// 손님이 멈췄거나 기록된 경로가 짧으면 주변 대기 위치를 사용
	if (!bHasTrailTarget)
	{
		Target = GetParkTarget(Foot, Current);
	}

	// 목표까지의 수평 방향과 거리를 계산
	FVector ToTarget = Target - Current;
	ToTarget.Z = 0.0f;
	const float Distance = ToTarget.Size();

	// 손님의 최대 걷기 속도를 조회하고, 이동 컴포넌트가 없으면 기본값 300을 사용
	const UCharacterMovementComponent* CustomerMovement = Customer->GetCharacterMovement();
	float CustomerMaxSpeed = 300.0f;
	if (CustomerMovement != nullptr)
	{
		CustomerMaxSpeed = CustomerMovement->MaxWalkSpeed;
	}

	// 손님 최대 걷기 속도에 추적 배율을 곱해 기본 최고 속도를 계산
	const float MaxSpeed = CustomerMaxSpeed * FollowSpeedRatio;

	// 정지 범위 안에서는 목표 이동 속력을 0으로 유지
	float DesiredSpeed = 0.0f;
	if (Distance > StopRadius)
	{
		// 목표에 가까워질수록 속력을 낮추고 기본 최고 속도 이하로 제한
		DesiredSpeed = FMath::Min(MaxSpeed, Distance * ArrivalSpeedGain);

		// 목표가 멀면 제한된 속력에 따라잡기 배율을 추가 적용
		if (Distance > CatchUpDistance)
		{
			DesiredSpeed *= CatchUpSpeedMultiplier;
		}
	}

	// 목표 방향에 속력을 곱해 이번 프레임에 지향할 이동 속도 벡터를 계산
	FVector DesiredVelocity = FVector::ZeroVector;
	if (Distance > KINDA_SMALL_NUMBER)
	{
		DesiredVelocity = ToTarget / Distance * DesiredSpeed;
	}

	// 현재 속도에서 목표 속도로 바뀌는 양을 제한
	// 가속, 감속, 방향 전환이 한 번에 적용되지 않도록 처리
	FollowVelocity = FMath::VInterpConstantTo(FollowVelocity, DesiredVelocity, DeltaSeconds, FollowAcceleration);
	FollowVelocity.Z = 0.0f;

	// 속도에 프레임 시간을 곱해 이번 프레임의 이동량을 계산
	FVector Delta = FollowVelocity * DeltaSeconds;

	// 속도에 프레임 시간을 곱해 이번 프레임의 이동량을 계산
	if (Delta.SizeSquared2D() > FMath::Square(Distance))
	{
		Delta = ToTarget;
	}

	// 화면에 보이는 보간 높이 대신 계산된 바닥 높이를 충돌 검사에 사용
	// 계단을 오르는 동안 낮게 남아 있는 표시 높이 때문에 막히는 현상을 줄임
	FVector MoveFrom = Current;
	if (bFloorZValid)
	{
		MoveFrom.Z = FloorZ;
	}

	// NavMesh 경계와 장애물 충돌을 반영한 이동 위치를 계산
	FVector NewLocation = ConstrainMove(MoveFrom, Delta);

	// 충돌 처리 후 실제로 이동할 수 있는 수평 이동량을 계산.
	FVector ActualMove = NewLocation - MoveFrom;
	ActualMove.Z = 0.0f;

	// 예정 이동 거리의 90% 미만으로 움직였다면 실제 이동량으로 속도를 보정
	// 막힌 방향으로 계속 밀어붙이는 속도를 줄이고 실제 이동 방향을 반영
	if (ActualMove.SizeSquared() < Delta.SizeSquared2D() * 0.81f)
	{
		FollowVelocity = ActualMove / DeltaSeconds;
	}

	// 이동할 위치의 바닥 높이를 계산하고 표시 높이를 갱신
	// 앞쪽 바닥 검사는 충돌 처리 후 보정된 이동 방향을 사용
	NewLocation.Z = UpdateFloorZ(NewLocation, Foot, DeltaSeconds);

	// 거의 정지한 상태에서는 손님 방향을 바라봄
	float DesiredYaw = Customer->GetActorRotation().Yaw;

	// 수평 이동 속력이 5보다 크면 실제 이동 방향을 바라봄
	if (FollowVelocity.SizeSquared2D() > 25.0f)
	{
		DesiredYaw = FollowVelocity.Rotation().Yaw;
	}

	// 현재 회전에서 목표 회전으로 부드럽게 변경
	const FRotator NewRotation = FMath::RInterpTo(
		GetActorRotation(),
		FRotator(0.0f, DesiredYaw, 0.0f),
		DeltaSeconds,
		RotationInterpSpeed);

	SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::None);

	// 갱신 위치를 기준으로 끼임 시간을 계산
	// 목표 거리는 이동 전 값이며, 순간이동 여부는 다음 Tick에서 확인
	UpdateStuck(NewLocation, Distance, DeltaSeconds);
}

bool ACSCustomerBasketActor::ComputeTrailTarget(const FVector& CustomerFoot,float FollowDistance,FVector& OutTarget) const
{
	// 현재 발 위치 → 최신 발자국 → ... → 오래된 발자국 순으로 경로 길이를 쌓습니다.
	float Remaining = FollowDistance;
	FVector Previous = CustomerFoot;

	// 현재 발 위치에서 최신 발자국부터 오래된 발자국 순으로 확인
	for (int32 Index = TrailPoints.Num() - 1; Index >= 0; --Index)
	{
		const FVector& Point = TrailPoints[Index];
		const float SegmentLength = FVector::Dist2D(Previous, Point);

		// 이번 경로 구간 안에 목표가 있으면 남은 거리 비율로 위치를 계산
		if (SegmentLength >= Remaining && SegmentLength > KINDA_SMALL_NUMBER)
		{
			OutTarget = Previous + (Point - Previous) * (Remaining / SegmentLength);
			return true;
		}

		// 확인한 구간 길이를 빼고 다음 발자국으로 이동
		Remaining -= SegmentLength;
		Previous = Point;
	}

	// 기록된 경로 길이가 추적 거리보다 짧으면 실패
	return false;
}

FVector ACSCustomerBasketActor::GetParkTarget(const FVector& CustomerFoot, const FVector& CurrentLocation)
{
	const UWorld* World = GetWorld();

	float Now = 0.0f;
	if (World != nullptr)
	{
		Now = World->GetTimeSeconds();
	}

	// 저장된 위치가 없거나 손님이 멀어졌거나 탐색 주기가 지나면 재탐색
	const bool bShouldSearch = !bParkTargetCached ||
		FVector::DistSquared2D(CustomerFoot, ParkSearchCustomerFoot) > FMath::Square(ReParkDistance) ||
		Now - ParkSearchTime >= ReParkInterval;

	if (bShouldSearch)
	{
		// 대기 위치를 찾지 못하면 현재 장바구니 위치를 유지
		FVector Found;

		if (FindParkLocation(Found))
		{
			CachedParkTarget = Found;
		}
		else
		{
			CachedParkTarget = CurrentLocation;
		}

		// 이번 탐색의 기준 위치와 시간을 저장
		ParkSearchCustomerFoot = CustomerFoot;
		ParkSearchTime = Now;
		bParkTargetCached = true;
	}
	return CachedParkTarget;
}

bool ACSCustomerBasketActor::FindParkLocation(FVector& OutLocation) const
{
	const ACSCustomerCharacter* Customer = FollowTarget.Get();
	UWorld* World = GetWorld();

	if (!IsValid(Customer) || World == nullptr)
	{
		return false;
	}

	const UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	const FVector Foot = GetCustomerFootLocation();
	const bool bFootOnNavMesh = IsOnNavMesh(Foot);

	// 손님과 장바구니의 반경에 대기 간격을 더해 중심 사이 거리를 계산
	const float ParkDistance = GetCustomerBodyRadius() + BasketRadius + ParkGap;
	const float CustomerYaw = Customer->GetActorRotation().Yaw;

	// 후보 주변에서 NavMesh 위치를 찾을 수평 범위와 높이 범위를 설정
	const FVector ProjectExtent(ParkNavProjectTolerance, ParkNavProjectTolerance, NavProjectHeight);

	for (const float CandidateYaw : ParkCandidateYaws)
	{
		// 손님이 바라보는 방향에 후보 각도를 더해 대기 위치를 계산
		const FVector Direction = FRotator(0.0f, CustomerYaw + CandidateYaw, 0.0f).Vector();
		FVector Candidate = Foot + Direction * ParkDistance;
		bool bPassed = true;

		// 손님이 NavMesh 위에 있으면 후보 위치와 직선 이동 가능 여부를 검사
		if (bFootOnNavMesh && NavSys != nullptr)
		{
			// 1) 후보가 NavMesh 위(허용 오차 이내)에 있고
			// 2) 손님 발에서 NavMesh를 벗어나지 않고 닿아야 통과
			FNavLocation Projected;
			bPassed = NavSys->ProjectPointToNavigation(Candidate, Projected, ProjectExtent);
			if (bPassed)
			{
				// 찾은 NavMesh 위치의 수평 좌표를 후보에 반영
				Candidate.X = Projected.Location.X;
				Candidate.Y = Projected.Location.Y;
				bPassed = IsNavReachable(Foot, Candidate);
			}
		}

		// 손님 위치에서 후보까지 구체 스윕으로 장애물을 검사
		// 손님이 NavMesh 밖에 있으면 이 충돌 검사만 적용
		if (bPassed)
		{
			FHitResult Hit;
			bPassed = !IsSweepBlocked(Foot, Candidate, Hit);
		}

		// 후보 높이는 손님 발 높이를 사용하고 실제 바닥 높이는 이동 시 계산
		Candidate.Z = Foot.Z;

		if (bPassed)
		{
			OutLocation = Candidate;
			return true;
		}
	}
	return false;
}

FVector ACSCustomerBasketActor::ConstrainMove(const FVector& From, const FVector& Delta)
{
	const FVector HorizontalDelta(Delta.X, Delta.Y, 0.0f);

	// 수평 이동량이 거의 없으면 현재 위치를 유지
	if (HorizontalDelta.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		return From;
	}

	FVector To = From + HorizontalDelta;

	// NavMesh 위에서 출발할 때만 경계 제한과 미끄러짐을 적용
	// 이미 NavMesh 밖에 있으면 이 검사를 건너뜀
	if (IsOnNavMesh(From))
	{
		To = NavSlideMove(From, To);
	}

	// 시작 위치에서 보정된 최종 위치까지 구체 스윕으로 장애물을 검사
	To = SweepMove(From, To);
	return To;
}

FVector ACSCustomerBasketActor::NavSlideMove(const FVector& From, const FVector& To) const
{
	FVector Start = From;
	FVector End(To.X, To.Y, From.Z);

	// 최초 이동 검사 후 설정한 횟수만큼 미끄러짐 경로를 추가 검사
	// 0이면 최초 경계 앞에서 정지, 1이면 미끄러짐 경로를 한 번 더 검사
	for (int32 Iteration = 0; Iteration <= MaxNavSlideIterations; ++Iteration)
	{
		FVector HitLocation;
		FVector HitNormal;

		// 경계에 막히지 않으면 이번 검사 목표까지 이동
		if (!NavRaycast(Start, End, HitLocation, HitNormal))
		{
			return End;
		}

		FVector Move = End - Start;
		Move.Z = 0.0f;
		const float MoveLength = Move.Size();
		if (MoveLength <= KINDA_SMALL_NUMBER)
		{
			return Start;
		}
		const FVector Direction = Move / MoveLength;

		// 경계 법선을 얻지 못하면 미끄러질 방향을 계산하지 않고 경계 앞에서 정지
		if (HitNormal.IsNearlyZero())
		{
			const float HitLength = static_cast<float>(FVector::Dist2D(Start, HitLocation));
			FVector Stopped = Start + Direction * FMath::Clamp(HitLength - NavHitBackOff, 0.0f, MoveLength);
			Stopped.Z = From.Z;
			return Stopped;
		}

		// 경계 법선이 이동 방향과 반대쪽을 향하도록 조정
		if (FVector::DotProduct(HitNormal, Direction) > 0.0f)
		{
			HitNormal = -HitNormal;
		}

		// 경계에 붙어서 재충돌하지 않도록 법선 방향으로 여유 거리를 확보
		FVector Reached = HitLocation + HitNormal * NavHitBackOff;
		Reached.Z = From.Z;

		// 여유 거리 보정 때문에 시작 위치보다 뒤로 밀리면 시작 위치를 유지
		if (FVector::DotProduct(Reached - Start, Direction) < 0.0f)
		{
			Reached = Start;
		}

		// 남은 이동량에서 경계를 밀어내는 성분을 제거
		// 경계와 나란한 방향의 이동량만 남겨 미끄러짐에 사용
		FVector Remain = End - Reached;
		Remain.Z = 0.0f;
		Remain -= FVector::DotProduct(Remain, HitNormal) * HitNormal;

		// 추가 검사 횟수를 다 썼거나 남은 이동량이 작으면 경계 앞에서 종료
		if (Iteration == MaxNavSlideIterations || Remain.SizeSquared2D() < 1.0f)
		{
			return Reached;
		}

		// 경계 앞에서 미끄러질 위치까지 다음 이동 검사를 진행
		Start = Reached;
		End = Reached + Remain;
	}
	return Start;
}

bool ACSCustomerBasketActor::NavRaycast(const FVector& From,const FVector& To,FVector& OutHitLocation,FVector& OutHitNormal) const
{
	// 충돌 위치는 목표 위치로 초기화하고 법선은 없는 상태로 설정
	OutHitLocation = To;
	OutHitNormal = FVector::ZeroVector;

	// 현재 월드의 내비게이션 시스템을 조회
	UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSys = nullptr;
	if (World != nullptr)
	{
		NavSys = UNavigationSystemV1::GetCurrent(World);
	}

	// 기본 내비게이션 데이터가 Recast NavMesh인지 확인
	const ARecastNavMesh* NavMesh = nullptr;
	if (NavSys != nullptr)
	{
		NavMesh = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance());
	}

	if (NavMesh != nullptr)
	{
		// 시작 위치에서 목표 위치까지 NavMesh 경계 충돌을 검사
		// 충돌 위치는 OutHitLocation에, 경계 법선은 Result에 저장
		ARecastNavMesh::FRaycastResult Result;
		const bool bHit = ARecastNavMesh::NavMeshRaycast(
			NavMesh,
			From,
			To,
			OutHitLocation,
			NavMesh->GetDefaultQueryFilter(),
			this,
			Result);
		if (bHit)
		{
			// 경계 법선의 높이 성분을 제거하고 길이를 1로 정규화
			// 이동 함수에서 경계를 따라 미끄러질 방향을 계산할 때 사용
			OutHitNormal = FVector(Result.HitNormal.X, Result.HitNormal.Y, 0.0f).GetSafeNormal();
		}
		// 경계에 막혔으면 true, 막히지 않았으면 false 반환
		return bHit;
	}

	// Recast NavMesh를 얻지 못하면 일반 내비게이션 레이캐스트로 검사
	// 법선은 0으로 유지되어 호출한 이동 함수에서 미끄러짐 없이 정지 처리
	return UNavigationSystemV1::NavigationRaycast(World, From, To, OutHitLocation);
}

FVector ACSCustomerBasketActor::SweepMove(const FVector& From, const FVector& To) const
{
	// 장애물이 없으면 목표 위치까지 이동
	FHitResult Hit;
	if (!IsSweepBlocked(From, To, Hit))
	{
		return To;
	}

	// 스윕 구체 중심에서 띄운 높이를 빼 장바구니 기준 위치로 환산
	const FVector Lift(0.0f, 0.0f, GetSweepLift());
	FVector HitLocation = Hit.Location - Lift;
	HitLocation.Z = From.Z;

	// 남은 수평 이동량에서 벽면 법선 방향 성분을 제거
	FVector Remain = To - HitLocation;
	Remain.Z = 0.0f;
	const FVector Normal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.0f).GetSafeNormal();
	Remain -= FVector::DotProduct(Remain, Normal) * Normal;

	// 벽면을 따라 이동할 성분이 없으면 충돌 위치에서 정지
	if (Remain.IsNearlyZero())
	{
		return HitLocation;
	}

	FVector SlideTo = HitLocation + Remain;
	FHitResult SlideHit;

	// 미끄러지는 경로에서 다시 막히면 두 번째 충돌 위치까지만 이동
	if (IsSweepBlocked(HitLocation, SlideTo, SlideHit))
	{
		SlideTo = SlideHit.Location - Lift;
		SlideTo.Z = From.Z;
	}

	// 벽면 미끄러짐이 NavMesh 경계를 넘으면 첫 충돌 위치에서 정지
	if (IsOnNavMesh(HitLocation) && !IsNavReachable(HitLocation, SlideTo))
	{
		return HitLocation;
	}
	return SlideTo;
}

bool ACSCustomerBasketActor::IsSweepBlocked(const FVector& From, const FVector& To, FHitResult& OutHit) const
{
	UWorld* World = GetWorld();
	if (World == nullptr || SweepObjectTypes.IsEmpty() || FVector::DistSquared2D(From, To) <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// 장바구니 자신과 소유 액터를 충돌 검사에서 제외
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BasketFollowSweep), false, this);
	if (AActor* OwnerActor = GetOwner())
	{
		QueryParams.AddIgnoredActor(OwnerActor);
	}

	// 장바구니에 부착한 표시 상품도 검사에서 제외
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	QueryParams.AddIgnoredActors(AttachedActors);

	const FCollisionObjectQueryParams ObjectParams(SweepObjectTypes);

	// 바닥과 낮은 턱을 피하도록 검사 구체를 띄워 수평 이동 검사
	const FVector Lift(0.0f, 0.0f, GetSweepLift());
	FVector Start = From + Lift;
	FVector End = To + Lift;
	End.Z = Start.Z;

	if (!World->SweepSingleByObjectType(
		OutHit,
		Start,
		End,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(BasketRadius),
		QueryParams))
	{
		return false;
	}

	// 시작부터 겹친 상태는 이동 차단으로 처리하지 않음
	// 바닥이나 완만한 경사면을 제외하고 벽처럼 가파른 면만 차단
	return !OutHit.bStartPenetrating && OutHit.ImpactNormal.Z < WalkableNormalZ;
}

bool ACSCustomerBasketActor::IsOnNavMesh(const FVector& Location) const // 허용 검색 범위 안에서 NavMesh 위치를 찾을 수 있는지 확인
{
	UWorld* World = GetWorld();
	const UNavigationSystemV1* NavSys = World != nullptr ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (NavSys == nullptr)
	{
		return false;
	}

	FNavLocation Projected;
	const FVector QueryExtent(OnNavMeshQueryTolerance, OnNavMeshQueryTolerance, NavProjectHeight);
	return NavSys->ProjectPointToNavigation(Location, Projected, QueryExtent);
}

bool ACSCustomerBasketActor::IsNavReachable(const FVector& From, const FVector& To, FVector* OutHit) const
{
	FVector HitLocation = To;
	const bool bHit = UNavigationSystemV1::NavigationRaycast(GetWorld(), From, To, HitLocation);
	// 충돌 위치를 요청한 경우 검사 결과 위치를 전달
	if (OutHit != nullptr)
	{
		*OutHit = HitLocation;
	}
	return !bHit;
}

bool ACSCustomerBasketActor::TraceFloorZ(const FVector& Location,float SeedZ,float UpHeight,float& OutFloorZ) const
{
	UWorld* World = GetWorld();
	if (World == nullptr || FloorTraceObjectTypes.IsEmpty())
	{
		return false;
	}

	// 장바구니 자신과 소유 액터를 바닥 검사에서 제외
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BasketFloorTrace), false, this);
	if (AActor* OwnerActor = GetOwner())
	{
		QueryParams.AddIgnoredActor(OwnerActor);
	}

	// 장바구니에 부착한 표시 상품을 바닥으로 인식하지 않도록 제외
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	QueryParams.AddIgnoredActors(AttachedActors);

	// 기준 높이 위에서 시작해 설정한 아래쪽 거리까지 수직 검사
	const FVector Start(Location.X, Location.Y, SeedZ + UpHeight);
	const FVector End(Location.X, Location.Y, SeedZ - FloorTraceDownDistance);

	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(
		Hit,
		Start,
		End,
		FCollisionObjectQueryParams(FloorTraceObjectTypes),
		QueryParams))
	{
		return false;
	}

	// 물체 내부에서 시작했거나 벽처럼 가파른 면이면 바닥으로 사용하지 않음
	if (Hit.bStartPenetrating || Hit.ImpactNormal.Z < WalkableNormalZ)
	{
		return false;
	}

	OutFloorZ = static_cast<float>(Hit.ImpactPoint.Z);
	return true;
}

float ACSCustomerBasketActor::UpdateFloorZ(const FVector& NewLocation,const FVector& CustomerFoot,float DeltaSeconds)
{
	// 저장된 바닥 높이가 없으면 현재 위치에서 즉시 계산
	if (!bFloorZValid)
	{
		SnapFloorZ(NewLocation, static_cast<float>(CustomerFoot.Z));
		return VisualZ;
	}

	// 마지막 검사 위치에서 수평으로 0.5 이상 움직였을 때만 바닥을 재검사
	// 정지 중에는 저장된 바닥 높이를 사용
	if (FVector::DistSquared2D(NewLocation, LastFloorTraceLocation) >= FMath::Square(0.5f))
	{
		LastFloorTraceLocation = NewLocation;

		float CenterZ = 0.0f;
		if (TraceFloorZ(NewLocation, FloorZ, FloorStepUpHeight, CenterZ))
		{
			float TargetZ = CenterZ;

			// 현재 속력과 미리 보기 시간으로 앞쪽 바닥 검사 거리를 계산
			// 장바구니 반경보다 멀리 검사하지 않도록 제한
			const float LeadDistance = FMath::Min(FollowVelocity.Size2D() * FloorProbeLeadTime, BasketRadius);
			if (LeadDistance > 1.0f)
			{
				const FVector Ahead = NewLocation + FollowVelocity.GetSafeNormal2D() * LeadDistance;
				float AheadZ = 0.0f;
				// 앞쪽 바닥이 더 높으면 계단의 수직면에 닿기 전에 높이를 올림
				// 앞쪽이 더 낮으면 중심 위치의 바닥 높이를 유지
				if (TraceFloorZ(Ahead, CenterZ, FloorStepUpHeight, AheadZ) && AheadZ > CenterZ)
				{
					TargetZ = AheadZ;
				}
			}
			FloorZ = TargetZ;
		}
		else
		{
			// 검사 범위에서 바닥을 찾지 못하면 손님 발 높이를 사용
			FloorZ = static_cast<float>(CustomerFoot.Z);
		}
	}

	// 표시 높이가 계산된 바닥 높이를 부드럽게 따라가도록 보간
	if (FloorZInterpSpeed > 0.0f)
	{
		VisualZ = FMath::FInterpTo(
			VisualZ, FloorZ, DeltaSeconds, FloorZInterpSpeed);
	}
	else
	{
		// 보간 속도가 0 이하이면 계산된 높이를 즉시 적용
		VisualZ = FloorZ;
	}
	return VisualZ;
}

void ACSCustomerBasketActor::SnapFloorZ(const FVector& Location, float SeedZ)
{
	float FoundZ = SeedZ;
	// 초기 배치용 탐색 범위로 바닥을 찾고 실패하면 기준 높이를 사용
	if (!TraceFloorZ(Location, SeedZ, FloorSnapSearchUp, FoundZ))
	{
		FoundZ = SeedZ;
	}
	// 실제 검사 높이와 화면 표시 높이를 같은 값으로 설정
	FloorZ = FoundZ;
	VisualZ = FoundZ;
	LastFloorTraceLocation = Location;
	bFloorZValid = true;
}

float ACSCustomerBasketActor::GetSweepLift() const
{
	// 구체 아래쪽이 바닥에서 계단 허용 높이만큼 떨어지도록 설정
	// 낮은 계단 수직면에 이동 스윕이 걸리는 현상을 줄임
	return BasketRadius + FloorStepUpHeight;
}

void ACSCustomerBasketActor::RecordTrail(const FVector& CustomerFoot)
{
	// 마지막 발자국과의 수평 거리가 기록 간격보다 짧으면 생략
	if (!TrailPoints.IsEmpty() && FVector::DistSquared2D(TrailPoints.Last(), CustomerFoot) < FMath::Square(TrailPointSpacing))
	{
		return;
	}

	TrailPoints.Add(CustomerFoot);

	// 저장 개수를 넘으면 오래된 발자국부터 제거
	// 설정값이 작아도 최소 4개의 기록은 유지
	const int32 Overflow = TrailPoints.Num() - FMath::Max(MaxTrailPoints, 4);
	if (Overflow > 0)
	{
		TrailPoints.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

void ACSCustomerBasketActor::UpdateStuck(const FVector& NewLocation, float TargetDistance, float DeltaSeconds)
{
	// 목표 근처에서 멈추는 것은 정상 동작이므로 끼임 판정에서 제외
	if (TargetDistance <= StuckMinTargetDistance)
	{
		StuckTime = 0.0f;
		StuckWindowElapsed = 0.0f;
		StuckWindowStartLocation = NewLocation;
		return;
	}

	// 설정한 검사 시간이 쌓일 때까지 대기
	StuckWindowElapsed += DeltaSeconds;
	if (StuckWindowElapsed < StuckCheckInterval)
	{
		return;
	}

	// 검사 시작 위치와 현재 위치 사이의 수평 거리를 계산
	// 이동 경로의 총길이가 아니므로 제자리 왕복도 진행 부족으로 판정 가능
	const float Progress = static_cast<float>(FVector::Dist2D(StuckWindowStartLocation, NewLocation));
	const float RequiredProgress = StuckMinProgressSpeed * StuckWindowElapsed;
	StuckTime = Progress < RequiredProgress ? StuckTime + StuckWindowElapsed : 0.0f;

	// 다음 검사 구간의 시작 시간과 위치를 갱신
	StuckWindowElapsed = 0.0f;
	StuckWindowStartLocation = NewLocation;
}

void ACSCustomerBasketActor::TeleportToPark(const TCHAR* Reason)
{
	const ACSCustomerCharacter* Customer = FollowTarget.Get();
	if (!IsValid(Customer))
	{
		return;
	}

	const FVector Foot = GetCustomerFootLocation();
	FVector Location;

	// 대기 가능한 위치가 없으면 손님 발 위치를 사용
	if (!FindParkLocation(Location))
	{
		Location = Foot;
	}
	// 새 위치의 바닥을 찾아 표시 높이까지 즉시 맞춤
	SnapFloorZ(Location, static_cast<float>(Foot.Z));
	Location.Z = VisualZ;

	// 손님이 바라보는 방향으로 회전을 맞춰 순간이동
	SetActorLocationAndRotation(
		Location,
		FRotator(0.0f, Customer->GetActorRotation().Yaw, 0.0f),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	// 이전 위치에서 사용한 속도와 경로를 지우고 현재 발 위치부터 다시 기록
	ResetFollowState();
	RecordTrail(Foot);

	UE_LOG(LogTemp, Verbose, TEXT("[장바구니] %s 순간이동 사유=%s 손님=%s"),*GetName(), Reason, *Customer->GetName());
}

void ACSCustomerBasketActor::ResetFollowState() // 이동 경로, 속도, 끼임 시간, 대기 위치 저장 상태를 초기화
{
	TrailPoints.Reset();
	FollowVelocity = FVector::ZeroVector;
	StuckTime = 0.0f;
	StuckWindowElapsed = 0.0f;
	StuckWindowStartLocation = GetActorLocation();
	IdleTime = 0.0f;
	bParkTargetCached = false;
}

FVector ACSCustomerBasketActor::GetCustomerFootLocation() const
{
	const ACSCustomerCharacter* Customer = FollowTarget.Get();
	if (!IsValid(Customer))
	{
		return GetActorLocation();
	}

	const UCapsuleComponent* Capsule = Customer->GetCapsuleComponent();

	float HalfHeight = 0.0f;
	if (Capsule != nullptr)
	{
		HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	}

	return Customer->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
}

float ACSCustomerBasketActor::GetCustomerBodyRadius() const
{
	const ACSCustomerCharacter* Customer = FollowTarget.Get();
	const UCapsuleComponent* Capsule = nullptr;
	if (IsValid(Customer))
	{
		Capsule = Customer->GetCapsuleComponent();
	}

	if (Capsule != nullptr)
	{
		return Capsule->GetScaledCapsuleRadius();
	}

	return 0.0f;
}

void ACSCustomerBasketActor::CollectDisplaySlots()
{
	if (!DisplaySlots.IsEmpty())
	{
		return;
	}

	TArray<UArrowComponent*> ArrowComponents;
	GetComponents<UArrowComponent>(ArrowComponents);
	TArray<TPair<int32, UArrowComponent*>> IndexedSlots;
	for (UArrowComponent* ArrowComponent : ArrowComponents)
	{
		int32 SlotIndex = INDEX_NONE;
		if (TryGetDisplaySlotIndex(ArrowComponent, SlotIndex))
		{
			IndexedSlots.Emplace(SlotIndex, ArrowComponent);
		}
	}
	IndexedSlots.Sort([](const TPair<int32, UArrowComponent*>& Left,
		const TPair<int32, UArrowComponent*>& Right)
	{
		return Left.Key < Right.Key;
	});
	for (const TPair<int32, UArrowComponent*>& IndexedSlot : IndexedSlots)
	{
		DisplaySlots.Add(IndexedSlot.Value);
		IndexedSlot.Value->SetHiddenInGame(true);
	}
}

bool ACSCustomerBasketActor::TryGetDisplaySlotIndex(
	const UArrowComponent* SlotMarker,
	int32& OutSlotIndex) const
{
	if (!IsValid(SlotMarker))
	{
		return false;
	}

	const FString Prefix = TEXT("BasketSlot_");
	const FString MarkerName = SlotMarker->GetName();
	if (!MarkerName.StartsWith(Prefix))
	{
		return false;
	}

	const FString IndexText = MarkerName.RightChop(Prefix.Len());
	if (IndexText.IsEmpty() || !IndexText.IsNumeric())
	{
		return false;
	}
	OutSlotIndex = FCString::Atoi(*IndexText);
	return OutSlotIndex >= 0;
}
