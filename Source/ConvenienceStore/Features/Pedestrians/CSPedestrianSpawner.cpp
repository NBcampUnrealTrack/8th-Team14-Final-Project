#include "Features/Pedestrians/CSPedestrianSpawner.h"

#include "Features/Pedestrians/CSPedestrianCharacter.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavigationSystemTypes.h"
#include "TimerManager.h"

// 활성 행인의 이동 입력을 처리할 Tick을 설정
ACSPedestrianSpawner::ACSPedestrianSpawner()
{
	// 활성 행인이 있을 때만 매 프레임 Tick을 실행
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

// -----------------------------------------------------------------------
// 생명주기
// -----------------------------------------------------------------------

// 난수 스트림과 이벤트 구독을 초기화하고 스폰 타이머를 시작
void ACSPedestrianSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 행인 전용 난수 스트림을 초기화
	// 손님 스트림을 사용하지 않으며 cs.Seed는 직접 읽지 않음
	PedestrianStream.Initialize(FMath::Rand());

	// 게임 상태를 보관하고 설정에 따라 마감 정산 이벤트를 구독
	CachedStoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (IsValid(CachedStoreGameState) && bReleaseAllOnDailySummary)
	{
		CachedStoreGameState->OnDailySummaryRequested.AddDynamic(
			this,
			&ACSPedestrianSpawner::HandleDailySummaryRequested);
	}

	// 필수 설정이 없으면 풀 준비와 타이머 시작을 중단
	if (PedestrianClass == nullptr ||
		EndpointA == nullptr ||
		EndpointB == nullptr ||
		Archetypes.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CSPedestrianSpawner] %s 설정 누락 - PedestrianClass / EndpointA / EndpointB / Archetypes를 확인하세요."),
			*GetName());
		return;
	}

	// 최대 배율이 최소 배율보다 작으면 실제 선택 시 최소 배율로 보정
	if (WalkSpeedMultiplierMax < WalkSpeedMultiplierMin)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSPedestrianSpawner] WalkSpeedMultiplierMax가 WalkSpeedMultiplierMin보다 작아 Min 값으로 고정됩니다."));
	}

	PrewarmPool();

	// 최소 0.1초 간격으로 스폰을 시도하며 첫 호출도 같은 간격 후 실행
	const float Interval = FMath::Max(SpawnInterval, 0.1f);
	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&ACSPedestrianSpawner::TrySpawnPedestrian,
		Interval,
		true,
		Interval);
}

// 타이머와 이벤트 구독을 해제하고 관리 중인 행인을 정리
void ACSPedestrianSpawner::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);

	if (IsValid(CachedStoreGameState))
	{
		CachedStoreGameState->OnDailySummaryRequested.RemoveDynamic(
			this,
			&ACSPedestrianSpawner::HandleDailySummaryRequested);
	}

	// 스포너가 파괴된 경우 활성 행인과 풀의 행인을 직접 파괴
	// 그 외 종료 사유에서는 여기서 Destroy를 호출하지 않음
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		for (const FCSActivePedestrian& Entry : ActivePedestrians)
		{
			if (IsValid(Entry.Pedestrian))
			{
				Entry.Pedestrian->Destroy();
			}
		}

		for (ACSPedestrianCharacter* Pooled : PooledPedestrians)
		{
			if (IsValid(Pooled))
			{
				Pooled->Destroy();
			}
		}
	}

	ActivePedestrians.Reset();
	PooledPedestrians.Reset();

	Super::EndPlay(EndPlayReason);
}

// 마감 정산 요청 시 현재 활성 행인을 모두 반납
// 스폰 타이머는 유지되므로 이후 생성 여부는 스폰 조건에 따라 결정
void ACSPedestrianSpawner::HandleDailySummaryRequested()
{
	ReleaseAll();
}

// -----------------------------------------------------------------------
// 생성
// -----------------------------------------------------------------------

// 생성 조건과 경로를 확인한 뒤 행인 한 명을 활성화
void ACSPedestrianSpawner::TrySpawnPedestrian()
{
	if (PedestrianClass == nullptr ||
		EndpointA == nullptr ||
		EndpointB == nullptr)
	{
		return;
	}

	// 생성 시간 제한을 사용하는 경우 손님 생성이 금지된 동안 스폰을 중단
	if (bSpawnOnlyWhenCustomersCanSpawn &&
		IsValid(CachedStoreGameState) &&
		CachedStoreGameState->bCanSpawnCustomers == false)
	{
		return;
	}

	// 마감 정산 UI가 열려 있는 동안에는 스폰하지 않음
	// 정산 UI는 게임을 일시정지하지 않아 타이머가 계속 호출되므로 여기서 차단
	// 정산 확인 후 CompleteDailySummary에서 false로 바뀌면 다시 스폰
	if (IsValid(CachedStoreGameState) &&
		CachedStoreGameState->bIsDailySummaryPending)
	{
		return;
	}

	// 이 스포너의 활성 행인 수가 상한에 도달하면 생성하지 않음
	if (ActivePedestrians.Num() >= FMath::Max(MaxAlivePedestrians, 0))
	{
		return;
	}

	// 등장 가중치에 따라 종족을 선택하고 외형 데이터 확인
	UCSCustomerArchetypeDataAsset* Archetype = PickArchetypeBySpawnWeight();
	if (Archetype == nullptr || Archetype->Visual == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSPedestrianSpawner] 종족 또는 Visual DA가 비어 있어 스폰을 건너뜁니다."));
		return;
	}

	// 종족 스케일로 스폰 범위, 높이, 코너 보정에 사용할 몸 크기를 산출
	const float CharacterScale = FMath::Max(Archetype->CharacterScale, 0.1f);
	float BodyRadius = 0.0f;
	float BodyHalfHeight = 0.0f;

	if (!GetBodySize(CharacterScale, BodyRadius, BodyHalfHeight))
	{
		return;
	}

	// A→B 또는 B→A 이동 방향을 무작위로 선택
	const bool bReverse = PedestrianStream.FRand() < 0.5f;
	const AActor* StartArea = EndpointA.Get();
	const AActor* EndArea = EndpointB.Get();

	if (bReverse)
	{
		StartArea = EndpointB.Get();
		EndArea = EndpointA.Get();
	}

	// 손님 스포너와 같이 출발 영역에서 위치를 1번만 뽑음
	// 다른 행인과의 간격은 검사하지 않으며, 영역이 유효하지 않으면 스포너 위치를 사용
	const FVector StartGround = PickGroundPointInArea(
		StartArea, GetActorLocation(), BodyRadius, BodyHalfHeight);

	// 반대쪽 영역에서 퇴장할 지면 좌표를 선택하고, 영역이 유효하지 않으면 출발 위치를 사용
	const FVector EndGround = PickGroundPointInArea(
		EndArea, StartGround, BodyRadius, BodyHalfHeight);

	// 출발지와 퇴장 위치를 연결하는 경로 점을 계산
	// 경로 탐색에 실패해도 스폰은 건너뛰지 않음
	TArray<FVector> PathPoints;
	BuildPathPoints(StartGround, EndGround, PathPoints);

	// 설정이 켜져 있으면 큰 몸에 맞춰 코너 경로 점을 보정
	if (bPushCornersForLargeBodies)
	{
		PushPathCornersOutward(PathPoints, BodyRadius);
	}

	// 첫 경로 구간을 바라보도록 회전 설정
	const FRotator Facing =
		(PathPoints[1] - PathPoints[0]).GetSafeNormal2D().Rotation();

	// 액터 원점은 캡슐 중심이므로 지면 좌표에 반높이를 더해 배치
	const FTransform SpawnTransform(
		Facing,
		StartGround + FVector(0.0f, 0.0f, BodyHalfHeight),
		FVector(CharacterScale));

	// 풀에서 행인을 꺼내거나 새로 생성
	ACSPedestrianCharacter* Pedestrian = AcquirePedestrian(SpawnTransform);
	if (!IsValid(Pedestrian))
	{
		return;
	}

	// 손님과 같은 방식으로 걷기 속도를 정하고 애니메이션 시작 비율을 무작위로 정해 활성화
	const float WalkSpeed = RollWalkSpeed();

	Pedestrian->ActivatePedestrian(
		Archetype->Visual,
		SpawnTransform,
		WalkSpeed,
		PedestrianStream.FRand());

	// 경로와 이동 상태를 활성 목록에 등록
	FCSActivePedestrian& Entry = ActivePedestrians.AddDefaulted_GetRef();
	Entry.Pedestrian = Pedestrian;
	Entry.PathPoints = MoveTemp(PathPoints);
	Entry.NextPointIndex = 1;
	Entry.LastProgressLocation = SpawnTransform.GetLocation();
	Entry.StuckElapsed = 0.0f;

	// 활성 행인의 이동 입력을 시작
	SetActorTickEnabled(true);
}

// 종족별 등장 가중치의 누적합을 기준으로 하나를 선택
UCSCustomerArchetypeDataAsset*
ACSPedestrianSpawner::PickArchetypeBySpawnWeight()
{
	// 비어 있는 항목은 제외하고 음수 가중치는 0으로 계산
	float TotalWeight = 0.0f;
	for (const UCSCustomerArchetypeDataAsset* Candidate : Archetypes)
	{
		if (Candidate != nullptr)
		{
			TotalWeight += FMath::Max(Candidate->SpawnWeightBase, 0.0f);
		}
	}

	// 양수 가중치가 없으면 첫 항목을 반환하고, 목록도 없으면 nullptr 반환
	if (TotalWeight <= 0.0f)
	{
		if (Archetypes.Num() > 0)
		{
			return Archetypes[0].Get();
		}

		return nullptr;
	}

	// 전체 가중치 범위에서 무작위 값을 선택
	const float Roll = PedestrianStream.FRandRange(0.0f, TotalWeight);
	float Cumulative = 0.0f;

	for (UCSCustomerArchetypeDataAsset* Candidate : Archetypes)
	{
		if (Candidate == nullptr)
		{
			continue;
		}

		// 누적 가중치가 선택한 값에 도달하면 해당 종족을 반환
		Cumulative += FMath::Max(Candidate->SpawnWeightBase, 0.0f);
		if (Roll <= Cumulative)
		{
			return Candidate;
		}
	}

	// 반복문에서 선택되지 않은 경우 마지막 항목을 반환
	return Archetypes.Last();
}

// 클래스 기본 캡슐 크기에 종족 스케일을 곱해 몸 크기를 반환
bool ACSPedestrianSpawner::GetBodySize(
	float CharacterScale,
	float& OutRadius,
	float& OutHalfHeight) const
{
	const ACSPedestrianCharacter* Template = PedestrianClass.GetDefaultObject();
	if (Template == nullptr || Template->GetCapsuleComponent() == nullptr)
	{
		return false;
	}

	const UCapsuleComponent* Capsule = Template->GetCapsuleComponent();
	OutRadius = Capsule->GetUnscaledCapsuleRadius() * CharacterScale;
	OutHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight() * CharacterScale;
	return true;
}

// 손님 스포너와 같은 방식으로 걷기 속도를 선택
// 손님: 새로 스폰한 액터의 MaxWalkSpeed(BP 기본값)에 0.85~1.15 배율을 곱함
// 행인: 풀에서 재사용하는 액터라 현재 값에 곱하면 배율이 누적되므로 기본 객체 값을 기준으로 계산
float ACSPedestrianSpawner::RollWalkSpeed()
{
	// TrySpawnPedestrian에서 GetBodySize가 기본 객체를 먼저 검사하므로 폴백은 방어용
	// 폴백 값은 ACSPedestrianCharacter 생성자 기본값과 동일
	float BaseWalkSpeed = 300.0f;

	const ACSPedestrianCharacter* Template = PedestrianClass.GetDefaultObject();
	if (Template != nullptr && Template->GetCharacterMovement() != nullptr)
	{
		BaseWalkSpeed = Template->GetCharacterMovement()->MaxWalkSpeed;
	}

	const float Multiplier = PedestrianStream.FRandRange(
		WalkSpeedMultiplierMin,
		FMath::Max(WalkSpeedMultiplierMin, WalkSpeedMultiplierMax));

	return BaseWalkSpeed * Multiplier;
}

// 영역 내부에서 후보 위치를 1번 뽑고 NavMesh에 투영한 지면 좌표를 반환
// 손님 스포너(ACSCustomerSpawner::PickPointInArea)와 같이 실패해도 폴백 좌표를 반환해 스폰을 건너뛰지 않음
FVector ACSPedestrianSpawner::PickGroundPointInArea(
	const AActor* AreaActor,
	const FVector& FallbackLocation,
	float BodyRadius,
	float BodyHalfHeight)
{
	// 영역이 유효하지 않으면 폴백 좌표를 사용
	if (!IsValid(AreaActor))
	{
		return FallbackLocation;
	}

	FVector Center = AreaActor->GetActorLocation();
	FQuat Rotation = FQuat::Identity;
	FVector Extent = FVector::ZeroVector;

	if (const UBoxComponent* Box =
		Cast<UBoxComponent>(AreaActor->GetRootComponent()))
	{
		// 루트가 박스이면 박스의 회전과 스케일이 반영된 범위를 사용
		Center = Box->GetComponentLocation();
		Rotation = Box->GetComponentQuat();
		Extent = Box->GetScaledBoxExtent();
	}
	else
	{
		// 박스 루트가 아니면 월드 축 기준 액터 경계 범위를 사용
		AreaActor->GetActorBounds(false, Center, Extent);
	}

	// 크기가 없는 지점 액터면 손님 스포너와 같이 액터 위치를 그대로 사용
	if (Extent.X <= KINDA_SMALL_NUMBER && Extent.Y <= KINDA_SMALL_NUMBER)
	{
		return AreaActor->GetActorLocation();
	}

	// 몸 반경만큼 안쪽으로 줄인 범위에서 후보를 추출
	const float LimitX = FMath::Max(Extent.X - BodyRadius, 0.0f);
	const float LimitY = FMath::Max(Extent.Y - BodyRadius, 0.0f);

	const FVector Local(
		PedestrianStream.FRandRange(-LimitX, LimitX),
		PedestrianStream.FRandRange(-LimitY, LimitY),
		0.0f);

	// 영역 회전을 반영해 로컬 후보를 월드 좌표로 변환
	const FVector Sampled = Center + Rotation.RotateVector(Local);

	const UNavigationSystemV1* NavSys =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());

	// 내비게이션 시스템이 없으면 투영 없이 추출한 좌표를 사용
	if (NavSys == nullptr)
	{
		return Sampled;
	}

	// 영역 높이와 몸 크기를 반영해 NavMesh 투영 검색 범위를 설정
	const FVector QueryExtent(
		FMath::Max(BodyRadius, 50.0f),
		FMath::Max(BodyRadius, 50.0f),
		FMath::Max(Extent.Z + BodyHalfHeight * 2.0f, 250.0f));

	FNavLocation Projected;
	if (!NavSys->ProjectPointToNavigation(Sampled, Projected, QueryExtent))
	{
		// 손님 스포너와 같이 경고를 남기고 영역 중심으로 폴백
		UE_LOG(LogTemp, Warning,
			TEXT("[CSPedestrianSpawner] %s 영역에서 뽑은 점이 NavMesh 밖이라 영역 중심을 사용합니다. 영역 배치와 NavMesh 범위를 확인하세요."),
			*AreaActor->GetName());
		return Center;
	}

	// 경로 계산용 좌표이므로 캡슐 반높이를 더하지 않고 반환
	return Projected.Location;
}

// -----------------------------------------------------------------------
// 경로 계산
// -----------------------------------------------------------------------

// 출발지와 퇴장 위치를 연결하는 NavMesh 경로 점을 계산
// 경로를 찾지 못하면 직선 경로, 부분 경로면 끝에 퇴장 위치를 이어 붙여 스폰을 건너뛰지 않음
// 진행하지 못하는 행인은 끼임 판정(StuckTimeoutSeconds)으로 반납
void ACSPedestrianSpawner::BuildPathPoints(
	const FVector& StartGround,
	const FVector& EndGround,
	TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();

	UNavigationSystemV1* NavSys =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());

	// 기존 기본 내비게이션 데이터를 사용하며 새로 생성하지 않음
	const ANavigationData* NavData = nullptr;
	if (NavSys != nullptr)
	{
		NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	}

	if (NavData != nullptr)
	{
		// 별도의 UNavigationPath 객체를 생성하는 편의 함수 대신 동기 쿼리를 사용
		FPathFindingQuery Query(this, *NavData, StartGround, EndGround);
		const FPathFindingResult Result = NavSys->FindPathSync(Query);

		// 출발점과 다음 이동 점이 필요하므로 최소 2개인 경로만 사용
		if (Result.IsSuccessful() &&
			Result.Path.IsValid() &&
			Result.Path->GetPathPoints().Num() >= 2)
		{
			// 경로 점의 위치만 이동용 배열에 복사
			const TArray<FNavPathPoint>& NavPoints = Result.Path->GetPathPoints();
			OutPoints.Reserve(NavPoints.Num() + 1);
			for (const FNavPathPoint& Point : NavPoints)
			{
				OutPoints.Add(Point.Location);
			}

			// 부분 경로면 경로 끝에서 퇴장 위치로 곧장 향하도록 이어 붙임
			if (Result.IsPartial())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[CSPedestrianSpawner] %s → %s 부분 경로입니다. 끝에 퇴장 위치를 이어 붙입니다. NavMeshBoundsVolume이 행인 길 전체를 덮는지 확인하세요."),
					*StartGround.ToCompactString(),
					*EndGround.ToCompactString());
				OutPoints.Add(EndGround);
			}

			return;
		}
	}

	// 경로를 찾지 못하면 출발지에서 퇴장 위치로 곧장 향하는 직선 경로를 사용
	UE_LOG(LogTemp, Warning,
		TEXT("[CSPedestrianSpawner] %s → %s 경로 탐색 실패. 직선 경로로 스폰합니다. NavMeshBoundsVolume이 행인 길 전체를 덮는지 확인하세요."),
		*StartGround.ToCompactString(),
		*EndGround.ToCompactString());

	OutPoints.Add(StartGround);
	OutPoints.Add(EndGround);
}

// 몸 반경이 기준 반경보다 크면 코너 바깥 방향으로 경로 점을 보정
void ACSPedestrianSpawner::PushPathCornersOutward(
	TArray<FVector>& Points,
	float BodyRadius) const
{
	// 기준보다 큰 반경만큼 보정하며, 중간 코너 점이 없으면 종료
	const float Extra = BodyRadius - NavAgentRadius;
	if (Extra <= KINDA_SMALL_NUMBER || Points.Num() < 3)
	{
		return;
	}

	const UNavigationSystemV1* NavSys =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());

	if (NavSys == nullptr)
	{
		return;
	}

	// 앞선 보정 결과가 다음 코너 계산에 영향을 주지 않도록 원본을 복사
	const TArray<FVector> Original = Points;

	for (int32 i = 1; i < Original.Num() - 1; ++i)
	{
		// 코너로 들어오는 방향과 나가는 방향을 수평 기준으로 계산
		const FVector InDir =
			(Original[i] - Original[i - 1]).GetSafeNormal2D();
		const FVector OutDir =
			(Original[i + 1] - Original[i]).GetSafeNormal2D();

		// 방향 차이를 사용해 회전 안쪽 방향을 계산
		const FVector InsideDir = OutDir - InDir;

		// 방향 변화가 거의 없는 직선 구간은 보정하지 않음
		if (InsideDir.SizeSquared2D() < 0.01f)
		{
			continue;
		}

		// 회전 안쪽의 반대 방향으로 부족한 반경만큼 이동
		const FVector Pushed =
			Original[i] - InsideDir.GetSafeNormal2D() * Extra;

		// 보정 위치를 NavMesh에 투영할 수 있을 때만 반영
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(
			Pushed, Projected, FVector(50.0f, 50.0f, 250.0f)))
		{
			Points[i] = Projected.Location;
		}
	}
}

// -----------------------------------------------------------------------
// 이동
// -----------------------------------------------------------------------

// 활성 행인의 이동을 갱신하고 퇴장하거나 끼인 행인을 반납
void ACSPedestrianSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 항목 제거 중 미처리 항목을 건너뛰지 않도록 뒤에서부터 순회
	for (int32 i = ActivePedestrians.Num() - 1; i >= 0; --i)
	{
		FCSActivePedestrian& Entry = ActivePedestrians[i];

		// 이미 사라진 행인은 활성 목록에서 제거
		if (!IsValid(Entry.Pedestrian))
		{
			ActivePedestrians.RemoveAtSwap(i, 1, EAllowShrinking::No);
			continue;
		}

		if (!UpdatePedestrian(Entry, DeltaSeconds))
		{
			ReleaseAt(i);
		}
	}

	// 이동할 행인이 없으면 다음 스폰까지 Tick을 중단
	if (ActivePedestrians.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}

// 경로 진행과 끼임을 확인하고 다음 점을 향해 이동 입력을 전달
// 경로 종료 또는 끼임으로 반납해야 하면 false를 반환
bool ACSPedestrianSpawner::UpdatePedestrian(
	FCSActivePedestrian& Entry,
	float DeltaSeconds)
{
	const FVector Location = Entry.Pedestrian->GetActorLocation();

	// 한 프레임에 여러 경로 점을 통과한 경우 모두 건너뜀
	while (Entry.PathPoints.IsValidIndex(Entry.NextPointIndex) &&
		HasPassedPoint(
			Entry.PathPoints[Entry.NextPointIndex - 1],
			Entry.PathPoints[Entry.NextPointIndex],
			Location))
	{
		++Entry.NextPointIndex;
	}

	// 마지막 경로 점까지 통과했으면 반납 대상으로 처리
	if (!Entry.PathPoints.IsValidIndex(Entry.NextPointIndex))
	{
		return false;
	}

	// 기준 위치에서 충분히 이동했으면 기준점과 누적 시간을 갱신
	if (FVector::DistSquared2D(Location, Entry.LastProgressLocation) >=
		FMath::Square(StuckMinMoveDistance))
	{
		Entry.LastProgressLocation = Location;
		Entry.StuckElapsed = 0.0f;
	}
	else
	{
		// 이동량이 부족한 시간을 누적하고 제한 시간에 도달하면 반납
		Entry.StuckElapsed += DeltaSeconds;

		if (Entry.StuckElapsed >= StuckTimeoutSeconds)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CSPedestrianSpawner] %s가 %.1f초 동안 진행하지 못해 반납합니다. 위치 %s"),
				*Entry.Pedestrian->GetName(),
				Entry.StuckElapsed,
				*Location.ToCompactString());
			return false;
		}
	}

	// 경로 점과 캡슐 중심의 높이 차이를 제외하고 수평 이동 방향을 계산
	const FVector Direction =
		(Entry.PathPoints[Entry.NextPointIndex] - Location).GetSafeNormal2D();

	Entry.Pedestrian->AddMovementInput(Direction, 1.0f);
	return true;
}

// 구간 진행 방향을 기준으로 목표 점까지 남은 거리를 검사
// 남은 거리가 PassLookahead 이하이면 다음 경로 점으로 넘어감
bool ACSPedestrianSpawner::HasPassedPoint(
	const FVector& From,
	const FVector& To,
	const FVector& Location) const
{
	const FVector2D Segment(To.X - From.X, To.Y - From.Y);
	const float SegmentLength = Segment.Size();

	// 길이가 거의 없는 구간은 통과한 것으로 처리
	if (SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	// 목표까지의 벡터를 구간 방향에 투영해 진행 방향의 남은 거리를 계산
	const FVector2D SegmentDir = Segment / SegmentLength;
	const FVector2D ToTarget(To.X - Location.X, To.Y - Location.Y);
	const float RemainingAlongSegment =
		FVector2D::DotProduct(ToTarget, SegmentDir);

	return RemainingAlongSegment <= PassLookahead;
}

// -----------------------------------------------------------------------
// 풀 관리
// -----------------------------------------------------------------------

// 풀에서 유효한 행인을 꺼내고, 없으면 새로 생성
ACSPedestrianCharacter* ACSPedestrianSpawner::AcquirePedestrian(
	const FTransform& SpawnTransform)
{
	// 사라진 참조는 건너뛰고 재사용 가능한 행인을 반환
	while (PooledPedestrians.Num() > 0)
	{
		ACSPedestrianCharacter* Pooled =
			PooledPedestrians.Pop(EAllowShrinking::No);

		if (IsValid(Pooled))
		{
			return Pooled;
		}
	}

	// 풀이 비어 있으면 새 액터를 생성
	return SpawnNewPedestrian(SpawnTransform);
}

// 풀을 거치지 않고 새 행인 액터를 생성
ACSPedestrianCharacter* ACSPedestrianSpawner::SpawnNewPedestrian(
	const FTransform& SpawnTransform)
{
	// 충돌에 의한 생성 취소 없이 새 액터를 생성
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACSPedestrianCharacter* Spawned =
		GetWorld()->SpawnActor<ACSPedestrianCharacter>(
			PedestrianClass, SpawnTransform, Params);

	if (IsValid(Spawned))
	{
		// 스포너가 입력을 전달한 뒤 이동 컴포넌트가 처리하도록 Tick 순서를 지정
		Spawned->GetCharacterMovement()->PrimaryComponentTick.AddPrerequisite(
			this, PrimaryActorTick);
	}

	return Spawned;
}

// 활성 목록에서 행인을 제거하고 비활성 상태로 풀에 반납
void ACSPedestrianSpawner::ReleaseAt(int32 ActiveIndex)
{
	ACSPedestrianCharacter* Pedestrian =
		ActivePedestrians[ActiveIndex].Pedestrian;

	ActivePedestrians.RemoveAtSwap(
		ActiveIndex, 1, EAllowShrinking::No);

	if (IsValid(Pedestrian))
	{
		Pedestrian->DeactivatePedestrian();
		PooledPedestrians.Add(Pedestrian);
	}
}

// 활성 행인을 모두 반납하고 이동 갱신 Tick을 중단
void ACSPedestrianSpawner::ReleaseAll()
{
	for (int32 i = ActivePedestrians.Num() - 1; i >= 0; --i)
	{
		ReleaseAt(i);
	}

	SetActorTickEnabled(false);
}

// 시작 시 PrewarmPoolSize만큼 새 행인을 생성하고 비활성 상태로 풀에 보관
// AcquirePedestrian을 쓰면 방금 넣은 액터를 다시 꺼내므로 풀을 거치지 않고 생성
void ACSPedestrianSpawner::PrewarmPool()
{
	for (int32 i = 0; i < PrewarmPoolSize; ++i)
	{
		ACSPedestrianCharacter* Pedestrian =
			SpawnNewPedestrian(GetActorTransform());

		if (IsValid(Pedestrian))
		{
			Pedestrian->DeactivatePedestrian();
			PooledPedestrians.Add(Pedestrian);
		}
	}
}
