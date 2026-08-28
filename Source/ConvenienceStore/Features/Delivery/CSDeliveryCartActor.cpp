#include "Features/Delivery/CSDeliveryCartActor.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Items/CSProductPickupActor.h"

ACSDeliveryCartActor::ACSDeliveryCartActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CartMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CartMesh"));
	CartMesh->SetupAttachment(SceneRoot);
	CartMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	CargoArea = CreateDefaultSubobject<UBoxComponent>(TEXT("CargoArea"));
	CargoArea->SetupAttachment(SceneRoot);
	CargoArea->SetBoxExtent(FVector(90.0f, 65.0f, 35.0f));
	CargoArea->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CargoArea->SetCollisionResponseToAllChannels(ECR_Overlap);
	CargoArea->SetGenerateOverlapEvents(true);
	CargoArea->OnComponentBeginOverlap.AddDynamic(this, &ACSDeliveryCartActor::HandleCargoBeginOverlap);
	CargoArea->OnComponentEndOverlap.AddDynamic(this, &ACSDeliveryCartActor::HandleCargoEndOverlap);

	CargoDropPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("CargoDropPoint"));
	CargoDropPoint->SetupAttachment(SceneRoot);
	// 기본 위치는 적재함 중심 위입니다. 블루프린트에서 수레 모델 높이에 맞춰 조정하시면 됩니다.
	CargoDropPoint->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));

	GroundTracePoint = CreateDefaultSubobject<UArrowComponent>(TEXT("GroundTracePoint"));
	GroundTracePoint->SetupAttachment(SceneRoot);
	// 기본값은 수레 중심 하단입니다. 바퀴 위치가 명확한 모델은 블루프린트에서 바퀴 사이로 옮기시면 됩니다.
	GroundTracePoint->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));

	FrontLeftGroundTracePoint = CreateDefaultSubobject<UArrowComponent>(TEXT("FrontLeftGroundTracePoint"));
	FrontLeftGroundTracePoint->SetupAttachment(SceneRoot);
	FrontLeftGroundTracePoint->SetRelativeLocation(FVector(100.0f, -60.0f, 0.0f));

	FrontRightGroundTracePoint = CreateDefaultSubobject<UArrowComponent>(TEXT("FrontRightGroundTracePoint"));
	FrontRightGroundTracePoint->SetupAttachment(SceneRoot);
	FrontRightGroundTracePoint->SetRelativeLocation(FVector(100.0f, 60.0f, 0.0f));

	RearLeftGroundTracePoint = CreateDefaultSubobject<UArrowComponent>(TEXT("RearLeftGroundTracePoint"));
	RearLeftGroundTracePoint->SetupAttachment(SceneRoot);
	RearLeftGroundTracePoint->SetRelativeLocation(FVector(-100.0f, -60.0f, 0.0f));

	RearRightGroundTracePoint = CreateDefaultSubobject<UArrowComponent>(TEXT("RearRightGroundTracePoint"));
	RearRightGroundTracePoint->SetupAttachment(SceneRoot);
	RearRightGroundTracePoint->SetRelativeLocation(FVector(-100.0f, 60.0f, 0.0f));

	DriverAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("DriverAnchor"));
	DriverAnchor->SetupAttachment(SceneRoot);
	// 캐릭터 Capsule 중심 위치입니다. 손 위치는 아래 양손 Anchor로 별도 보정합니다.
	DriverAnchor->SetRelativeLocation(FVector(-115.0f, 0.0f, 0.0f));

	LeftHandHandleAnchor = CreateDefaultSubobject<UArrowComponent>(TEXT("LeftHandHandleAnchor"));
	LeftHandHandleAnchor->SetupAttachment(SceneRoot);
	LeftHandHandleAnchor->SetRelativeLocation(FVector(-25.0f, -35.0f, 85.0f));

	RightHandHandleAnchor = CreateDefaultSubobject<UArrowComponent>(TEXT("RightHandHandleAnchor"));
	RightHandHandleAnchor->SetupAttachment(SceneRoot);
	RightHandHandleAnchor->SetRelativeLocation(FVector(-25.0f, 35.0f, 85.0f));

	DriverCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("DriverCamera"));
	DriverCamera->SetupAttachment(SceneRoot);
	DriverCamera->SetRelativeLocation(FVector(-420.0f, 0.0f, 230.0f));
	DriverCamera->SetRelativeRotation(FRotator(-16.0f, 0.0f, 0.0f));
	DriverCamera->bUsePawnControlRotation = false;
}

void ACSDeliveryCartActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsBeingDriven)
	{
		UpdateDrivingMovement(DeltaSeconds);
		// 이동 입력이 없는 순간에도 절벽·경사 변화에 반응하도록 매 프레임 바닥을 확인합니다.
		UpdateDrivingGroundHeight(DeltaSeconds);
		UpdateDrivingCameraRotation();
	}
}

bool ACSDeliveryCartActor::BeginDriving(AActor* NewDriver)
{
	if (!IsValid(NewDriver) || bIsBeingDriven)
	{
		return false;
	}

	DrivingActor = NewDriver;
	bIsBeingDriven = true;
	CurrentDriveSpeed = 0.0f;
	DriveForwardInput = 0.0f;
	DriveTurnInput = 0.0f;
	// 수레 기울기 보정으로 변하기 전의 카메라 상대 회전을 저장합니다.
	DefaultDriverCameraRelativeRotation = DriverCamera->GetRelativeRotation();
	// 운전자는 수레에 붙어 있으므로, 수레 메시와의 Pawn 충돌만 잠시 무시합니다.
	PreviousPawnCollisionResponse = CartMesh->GetCollisionResponseToChannel(ECC_Pawn);
	CartMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	// 운전을 시작하기 전에 한 번만 바닥에 정렬해 첫 프레임의 큰 높이 보정을 막습니다.
	UpdateDrivingGroundHeight(0.0f, true);
	return true;
}

void ACSDeliveryCartActor::StopDriving(AActor* RequestingDriver)
{
	if (!bIsBeingDriven || (IsValid(DrivingActor) && DrivingActor != RequestingDriver))
	{
		return;
	}

	CartMesh->SetCollisionResponseToChannel(ECC_Pawn, PreviousPawnCollisionResponse);
	// 운전 중 화면 수평 보정에 사용한 회전을 블루프린트 기본값으로 되돌립니다.
	DriverCamera->SetRelativeRotation(DefaultDriverCameraRelativeRotation);
	DrivingActor = nullptr;
	bIsBeingDriven = false;
	CurrentDriveSpeed = 0.0f;
	DriveForwardInput = 0.0f;
	DriveTurnInput = 0.0f;
}

void ACSDeliveryCartActor::Drive(float ForwardInput, float TurnInput)
{
	if (!bIsBeingDriven || GetWorld() == nullptr)
	{
		return;
	}

	// 입력은 저장만 하고, 실제 이동은 Tick에서 가속·감속을 적용해 처리합니다.
	DriveForwardInput = FMath::Clamp(ForwardInput, -1.0f, 1.0f);
	DriveTurnInput = FMath::Clamp(TurnInput, -1.0f, 1.0f);
}

void ACSDeliveryCartActor::UpdateDrivingMovement(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float TargetDriveSpeed = DriveForwardInput * DriveSpeed;
	const bool bIsAccelerating = FMath::Abs(TargetDriveSpeed) > FMath::Abs(CurrentDriveSpeed) &&
		(FMath::IsNearlyZero(CurrentDriveSpeed) ||
			FMath::Sign(TargetDriveSpeed) == FMath::Sign(CurrentDriveSpeed));
	const float SpeedChangeRate = bIsAccelerating ? DriveAcceleration : DriveDeceleration;
	CurrentDriveSpeed = FMath::FInterpConstantTo(
		CurrentDriveSpeed,
		TargetDriveSpeed,
		DeltaSeconds,
		SpeedChangeRate);

	// 후진할 때는 운전자 기준 좌우 조향 감각을 유지하도록 회전 부호를 반전합니다.
	const float EffectiveTurnInput = DriveForwardInput < -UE_KINDA_SMALL_NUMBER
		? -DriveTurnInput
		: DriveTurnInput;
	if (FMath::Abs(CurrentDriveSpeed) >= MinimumSteeringSpeed &&
		FMath::Abs(EffectiveTurnInput) > UE_KINDA_SMALL_NUMBER)
	{
		// 정지 중에는 조향 입력을 받아도 제자리 회전하지 않고, 이동을 시작한 뒤에만 방향을 바꿉니다.
		AddActorWorldRotation(FRotator(
			0.0f,
			EffectiveTurnInput * TurnSpeedDegreesPerSecond * DeltaSeconds,
			0.0f));
	}

	if (FMath::Abs(CurrentDriveSpeed) > UE_KINDA_SMALL_NUMBER)
	{
		// SceneRoot 구조를 유지하므로, 직접 Box Sweep을 실행해 벽과 문을 통과하지 않게 합니다.
		MoveWithDrivingCollision(GetActorForwardVector() * CurrentDriveSpeed * DeltaSeconds);
	}
}

void ACSDeliveryCartActor::UpdateDrivingCameraRotation()
{
	if (!IsValid(DriverCamera))
	{
		return;
	}

	const FRotator CartRotation = GetActorRotation();
	// 수레의 Pitch와 Yaw는 따라가되 World Roll을 0으로 고정해 화면이 좌우로 기울지 않게 합니다.
	const FRotator DesiredCameraRotation(
		CartRotation.Pitch + DefaultDriverCameraRelativeRotation.Pitch,
		CartRotation.Yaw + DefaultDriverCameraRelativeRotation.Yaw,
		0.0f);
	DriverCamera->SetWorldRotation(DesiredCameraRotation);
}

void ACSDeliveryCartActor::MoveWithDrivingCollision(const FVector& DesiredDelta)
{
	if (DesiredDelta.IsNearlyZero() || GetWorld() == nullptr)
	{
		return;
	}

	const FVector Start = GetActorTransform().TransformPosition(DrivingCollisionCenterOffset);
	const FVector End = Start + DesiredDelta;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DeliveryCartDriving), false, this);
	QueryParams.AddIgnoredActor(this);
	if (IsValid(DrivingActor))
	{
		QueryParams.AddIgnoredActor(DrivingActor);
	}

	const FCollisionShape CollisionShape = FCollisionShape::MakeBox(DrivingCollisionHalfExtent);
	const auto IsBlockingDrivingHit = [](const FHitResult& HitResult)
	{
		// DeliveryZone 같은 Overlap 전용 영역은 감지만 유지하고 수레 이동을 막지 않습니다.
		if (!HitResult.bBlockingHit ||
			Cast<ACSProductPickupActor>(HitResult.GetActor()) != nullptr)
		{
			return false;
		}

		const FVector SurfaceNormal = !HitResult.ImpactNormal.IsNearlyZero()
			? HitResult.ImpactNormal
			: HitResult.Normal;
		return SurfaceNormal.Z <= 0.5f;
	};

	const auto CalculateSafeMoveFraction = [&IsBlockingDrivingHit](const TArray<FHitResult>& HitResults)
	{
		float Result = 1.0f;
		for (const FHitResult& HitResult : HitResults)
		{
			if (!IsBlockingDrivingHit(HitResult))
			{
				continue;
			}

			Result = FMath::Min(
				Result,
				FMath::Clamp(HitResult.Time - 0.02f, 0.0f, 1.0f));
		}
		return Result;
	};

	TArray<FHitResult> HitResults;
	GetWorld()->SweepMultiByChannel(
		HitResults, Start, End, GetActorQuat(), ECC_Visibility, CollisionShape, QueryParams);
	const float SafeMoveFraction = CalculateSafeMoveFraction(HitResults);
	if (SafeMoveFraction >= 1.0f - UE_KINDA_SMALL_NUMBER)
	{
		SetActorLocation(GetActorLocation() + DesiredDelta, false);
		return;
	}

	if (MaxStepHeight > UE_KINDA_SMALL_NUMBER)
	{
		// 컴포넌트 전체 Bounds 대신 Step Up 뒤 실제로 착지할 바닥 높이를 검사합니다.
		// 이 방식은 넓은 평지 메시의 Bounds 때문에 낮은 턱까지 막히는 문제를 방지합니다.
		const float CartGroundHeight = GetActorLocation().Z - GroundClearance;
		const FVector StepDestination = GetActorLocation() + DesiredDelta;
		const float StepProbeHeight = MaxStepHeight + StepClearance + 20.0f;
		const FVector StepProbeStart = StepDestination + FVector(0.0f, 0.0f, StepProbeHeight);
		const FVector StepProbeEnd = StepDestination - FVector(0.0f, 0.0f, 20.0f);
		FHitResult StepLandingHit;
		const bool bFoundStepLanding = GetWorld()->LineTraceSingleByChannel(
			StepLandingHit,
			StepProbeStart,
			StepProbeEnd,
			ECC_Visibility,
			QueryParams);

		const FVector StepLandingNormal = !StepLandingHit.ImpactNormal.IsNearlyZero()
			? StepLandingHit.ImpactNormal
			: StepLandingHit.Normal;
		const float StepHeight = StepLandingHit.ImpactPoint.Z - CartGroundHeight;
		const bool bHasWalkableStepLanding = bFoundStepLanding &&
			!StepLandingHit.bStartPenetrating &&
			StepLandingNormal.Z > 0.5f &&
			StepHeight > -UE_KINDA_SMALL_NUMBER &&
			StepHeight <= MaxStepHeight + UE_KINDA_SMALL_NUMBER;

		if (!bHasWalkableStepLanding)
		{
			// 수직 벽이나 착지할 윗면이 없는 충돌체는 Step Up 없이 충돌 직전에서 멈춥니다.
			SetActorLocation(GetActorLocation() + DesiredDelta * SafeMoveFraction, false);
			return;
		}

		// 실제 턱 높이보다 조금 더 올려, 충돌 Box 하단이 턱 상단에 닿지 않게 합니다.
		const FVector StepOffset(0.0f, 0.0f, MaxStepHeight + StepClearance);
		TArray<FHitResult> StepHitResults;
		GetWorld()->SweepMultiByChannel(
			StepHitResults,
			Start + StepOffset,
			End + StepOffset,
			GetActorQuat(),
			ECC_Visibility,
			CollisionShape,
			QueryParams);
		const float StepMoveFraction = CalculateSafeMoveFraction(StepHitResults);
		if (StepMoveFraction >= 1.0f - UE_KINDA_SMALL_NUMBER)
		{
			// 높이가 MaxStepHeight 이하인 턱만 통과하고, 이후 바닥 추적으로 정확한 높이에 안착합니다.
			SetActorLocation(GetActorLocation() + StepOffset + DesiredDelta, false);
			return;
		}
	}

	// 턱도 넘을 수 없으면 원래 경로에서 충돌 직전까지만 이동합니다.
	SetActorLocation(GetActorLocation() + DesiredDelta * SafeMoveFraction, false);
}

void ACSDeliveryCartActor::UpdateDrivingGroundHeight(
	float DeltaSeconds,
	bool bForceSnapToGround)
{
	if (GetWorld() == nullptr || (DeltaSeconds <= 0.0f && !bForceSnapToGround))
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const FRotator CurrentRotation = GetActorRotation();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DeliveryCartGround), false, this);
	QueryParams.AddIgnoredActor(this);
	if (IsValid(DrivingActor))
	{
		QueryParams.AddIgnoredActor(DrivingActor);
	}
	for (const ACSProductPickupActor* CargoProduct : CargoProducts)
	{
		if (IsValid(CargoProduct))
		{
			// 적재 상품을 바닥으로 오인하면 수레 높이가 상품 위치에 맞춰져 덜컹거리게 됩니다.
			QueryParams.AddIgnoredActor(CargoProduct);
		}
	}

	// 상품(PhysicsBody)은 바닥 판정 대상이 아니므로, 월드 고정 오브젝트만 탐색합니다.
	// 적재 상품이 CargoArea에 등록되는 순간과 무관하게 수레가 상품 높이로 튀는 일을 막습니다.
	FCollisionObjectQueryParams GroundObjectParams;
	GroundObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	const auto TraceGroundAtPoint = [this, &GroundObjectParams, &QueryParams](
		const UArrowComponent* TracePoint,
		FHitResult& OutHit)
	{
		if (!IsValid(TracePoint))
		{
			return false;
		}

		const FVector TraceStart = TracePoint->GetComponentLocation() +
			FVector(0.0f, 0.0f, GroundTraceStartHeight);
		const FVector TraceEnd = TraceStart - FVector(0.0f, 0.0f, GroundTraceDistance);
		const bool bHitGround = GetWorld()->LineTraceSingleByObjectType(
			OutHit,
			TraceStart,
			TraceEnd,
			GroundObjectParams,
			QueryParams);
		if (!bHitGround)
		{
			return false;
		}

		// 벽 윗면처럼 현재 바퀴 위치보다 너무 높은 Hit는 바닥으로 취급하지 않습니다.
		// 낮은 턱과 완만한 경사만 MaxStepHeight 범위 안에서 수레 높이 보정에 사용합니다.
		const float CurrentGroundHeight = TracePoint->GetComponentLocation().Z - GroundClearance;
		const float HeightAboveCurrentGround = OutHit.ImpactPoint.Z - CurrentGroundHeight;
		return HeightAboveCurrentGround <= MaxStepHeight + UE_KINDA_SMALL_NUMBER;
	};

	FHitResult FrontLeftGroundHit;
	FHitResult FrontRightGroundHit;
	FHitResult RearLeftGroundHit;
	FHitResult RearRightGroundHit;
	const bool bHasFrontLeftGround = TraceGroundAtPoint(FrontLeftGroundTracePoint, FrontLeftGroundHit);
	const bool bHasFrontRightGround = TraceGroundAtPoint(FrontRightGroundTracePoint, FrontRightGroundHit);
	const bool bHasRearLeftGround = TraceGroundAtPoint(RearLeftGroundTracePoint, RearLeftGroundHit);
	const bool bHasRearRightGround = TraceGroundAtPoint(RearRightGroundTracePoint, RearRightGroundHit);
	if (!bHasFrontLeftGround && !bHasFrontRightGround &&
		!bHasRearLeftGround && !bHasRearRightGround && bForceSnapToGround)
	{
		return;
	}

	const auto GetAverageGroundHeight = [](const FHitResult& FirstHit, bool bHasFirstHit,
		const FHitResult& SecondHit, bool bHasSecondHit, float& OutHeight)
	{
		float HeightSum = 0.0f;
		int32 HitCount = 0;
		if (bHasFirstHit)
		{
			HeightSum += FirstHit.ImpactPoint.Z;
			++HitCount;
		}
		if (bHasSecondHit)
		{
			HeightSum += SecondHit.ImpactPoint.Z;
			++HitCount;
		}
		if (HitCount == 0)
		{
			return false;
		}
		OutHeight = HeightSum / HitCount;
		return true;
	};

	float FrontGroundHeight = 0.0f;
	float RearGroundHeight = 0.0f;
	float LeftGroundHeight = 0.0f;
	float RightGroundHeight = 0.0f;
	const bool bHasFrontGround = GetAverageGroundHeight(
		FrontLeftGroundHit, bHasFrontLeftGround,
		FrontRightGroundHit, bHasFrontRightGround, FrontGroundHeight);
	const bool bHasRearGround = GetAverageGroundHeight(
		RearLeftGroundHit, bHasRearLeftGround,
		RearRightGroundHit, bHasRearRightGround, RearGroundHeight);
	const bool bHasLeftGround = GetAverageGroundHeight(
		FrontLeftGroundHit, bHasFrontLeftGround,
		RearLeftGroundHit, bHasRearLeftGround, LeftGroundHeight);
	const bool bHasRightGround = GetAverageGroundHeight(
		FrontRightGroundHit, bHasFrontRightGround,
		RearRightGroundHit, bHasRearRightGround, RightGroundHeight);

	float TargetPitch = 0.0f;
	if (bHasFrontGround && bHasRearGround)
	{
		const FVector FrontCenter = (FrontLeftGroundTracePoint->GetComponentLocation() +
			FrontRightGroundTracePoint->GetComponentLocation()) * 0.5f;
		const FVector RearCenter = (RearLeftGroundTracePoint->GetComponentLocation() +
			RearRightGroundTracePoint->GetComponentLocation()) * 0.5f;
		const float FrontRearDistance = FVector::Dist2D(FrontCenter, RearCenter);
		if (FrontRearDistance > UE_KINDA_SMALL_NUMBER)
		{
			TargetPitch = FMath::RadiansToDegrees(FMath::Atan2(
				FrontGroundHeight - RearGroundHeight,
				FrontRearDistance));
		}
	}
	TargetPitch = FMath::Clamp(TargetPitch, -MaxGroundPitchDegrees, MaxGroundPitchDegrees);
	const float NextPitch = bForceSnapToGround
		? TargetPitch
		: FMath::FInterpTo(CurrentRotation.Pitch, TargetPitch, DeltaSeconds, GroundPitchInterpSpeed);

	float TargetRoll = 0.0f;
	if (bHasLeftGround && bHasRightGround)
	{
		const FVector LeftCenter = (FrontLeftGroundTracePoint->GetComponentLocation() +
			RearLeftGroundTracePoint->GetComponentLocation()) * 0.5f;
		const FVector RightCenter = (FrontRightGroundTracePoint->GetComponentLocation() +
			RearRightGroundTracePoint->GetComponentLocation()) * 0.5f;
		const float LeftRightDistance = FVector::Dist2D(LeftCenter, RightCenter);
		if (LeftRightDistance > UE_KINDA_SMALL_NUMBER)
		{
			// 수레 로컬 축 기준으로 왼쪽 바퀴가 높을 때 왼쪽이 들리도록 Roll 부호를 반전합니다.
			TargetRoll = FMath::RadiansToDegrees(FMath::Atan2(
				LeftGroundHeight - RightGroundHeight,
				LeftRightDistance));
		}
	}
	TargetRoll = FMath::Clamp(TargetRoll, -MaxGroundRollDegrees, MaxGroundRollDegrees);
	const float NextRoll = bForceSnapToGround
		? TargetRoll
		: FMath::FInterpTo(CurrentRotation.Roll, TargetRoll, DeltaSeconds, GroundRollInterpSpeed);
	const FRotator NextRotation(NextPitch, CurrentRotation.Yaw, NextRoll);

	TArray<float, TInlineAllocator<4>> TargetRootHeights;
	const auto AddRootHeightForGroundHit = [this, &NextRotation, &TargetRootHeights](
		const UArrowComponent* TracePoint,
		const FHitResult& GroundHit)
	{
		const FVector RotatedPointOffset = NextRotation.RotateVector(TracePoint->GetRelativeLocation());
		TargetRootHeights.Add(GroundHit.ImpactPoint.Z + GroundClearance - RotatedPointOffset.Z);
	};
	if (bHasFrontLeftGround)
	{
		AddRootHeightForGroundHit(FrontLeftGroundTracePoint, FrontLeftGroundHit);
	}
	if (bHasFrontRightGround)
	{
		AddRootHeightForGroundHit(FrontRightGroundTracePoint, FrontRightGroundHit);
	}
	if (bHasRearLeftGround)
	{
		AddRootHeightForGroundHit(RearLeftGroundTracePoint, RearLeftGroundHit);
	}
	if (bHasRearRightGround)
	{
		AddRootHeightForGroundHit(RearRightGroundTracePoint, RearRightGroundHit);
	}

	float TargetZ = CurrentLocation.Z - GroundFallSpeed * DeltaSeconds;
	if (!TargetRootHeights.IsEmpty())
	{
		float TargetRootHeightSum = 0.0f;
		for (const float TargetRootHeight : TargetRootHeights)
		{
			TargetRootHeightSum += TargetRootHeight;
		}
		TargetZ = TargetRootHeightSum / TargetRootHeights.Num();
	}
	const bool bNeedsHeightUpdate = FMath::Abs(TargetZ - CurrentLocation.Z) > GroundHeightTolerance;
	const bool bNeedsPitchUpdate = !FMath::IsNearlyEqual(CurrentRotation.Pitch, NextPitch, 0.01f);
	const bool bNeedsRollUpdate = !FMath::IsNearlyEqual(CurrentRotation.Roll, NextRoll, 0.01f);
	if (!bForceSnapToGround && !bNeedsHeightUpdate && !bNeedsPitchUpdate && !bNeedsRollUpdate)
	{
		return;
	}
	const float VerticalSpeed = TargetZ >= CurrentLocation.Z
		? GroundRiseSpeed
		: GroundFallSpeed;
	const float NextZ = bForceSnapToGround
		? TargetZ
		: FMath::FInterpConstantTo(
			CurrentLocation.Z,
			TargetZ,
			DeltaSeconds,
			VerticalSpeed);

	if (!FMath::IsNearlyEqual(CurrentLocation.Z, NextZ) || bNeedsPitchUpdate || bNeedsRollUpdate)
	{
		// 네 바퀴 바닥 높이를 Pitch·Roll에 반영해 지형을 따라 자연스럽게 기울입니다.
		SetActorLocationAndRotation(
			FVector(CurrentLocation.X, CurrentLocation.Y, NextZ),
			NextRotation,
			false);
	}
}

bool ACSDeliveryCartActor::LoadProduct(ACSProductPickupActor* Product)
{
	if (!IsValid(Product) || !IsValid(CargoDropPoint))
	{
		return false;
	}

	// 손에 들 때 중지한 물리를 먼저 초기화한 뒤, 적재함 위에서 다시 떨어뜨립니다.
	Product->PrepareForPickupPhysics();
	Product->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Product->SetActorLocation(
		CargoDropPoint->GetComponentLocation(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	Product->SetActorEnableCollision(true);
	Product->SetHeldByPlayerForFreshness(false);
	Product->EnableWorldDropPhysics();
	return true;
}

bool ACSDeliveryCartActor::LoadOneProductFromCrate(ACSDeliveryCrateActor* DeliveryCrate)
{
	if (!IsValid(DeliveryCrate) || DeliveryCrate->GetRemainingQuantity() < 1 ||
		!IsValid(CargoDropPoint))
	{
		return false;
	}

	// 상자 수량을 줄이기 전에 먼저 상품 액터 생성이 가능한지 확인합니다.
	ACSProductPickupActor* SpawnedProduct = DeliveryCrate->SpawnProductActor();
	if (!IsValid(SpawnedProduct))
	{
		return false;
	}

	// 생성 직후 적재함 위로 옮겨 물리를 켜므로, 상자에서 상품이 하나씩 떨어져 보입니다.
	SpawnedProduct->PrepareForPickupPhysics();
	SpawnedProduct->SetActorLocation(
		CargoDropPoint->GetComponentLocation(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	SpawnedProduct->SetActorEnableCollision(true);
	SpawnedProduct->SetHeldByPlayerForFreshness(false);
	SpawnedProduct->EnableWorldDropPhysics();

	if (!DeliveryCrate->ConsumeOne())
	{
		// 예외적으로 수량 차감에 실패하면 생성한 상품을 남기지 않습니다.
		SpawnedProduct->Destroy();
		return false;
	}
	return true;
}

TArray<ACSProductPickupActor*> ACSDeliveryCartActor::GetCargoProducts() const
{
	TArray<ACSProductPickupActor*> ValidProducts;
	for (ACSProductPickupActor* Product : CargoProducts)
	{
		if (IsValid(Product))
		{
			ValidProducts.Add(Product);
		}
	}
	return ValidProducts;
}

FTransform ACSDeliveryCartActor::GetCargoPreviewTransform() const
{
	if (IsValid(CargoDropPoint))
	{
		return CargoDropPoint->GetComponentTransform();
	}
	return GetActorTransform();
}

void ACSDeliveryCartActor::RemoveCargoProduct(ACSProductPickupActor* Product)
{
	CargoProducts.Remove(Product);
}

void ACSDeliveryCartActor::HandleCargoBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(OtherActor);
		IsValid(Product))
	{
		// 적재함에 들어온 상품은 고정하지 않고 참조만 기록하여, 흔들리면 다시 떨어질 수 있게 합니다.
		CargoProducts.AddUnique(Product);
	}
}

void ACSDeliveryCartActor::HandleCargoEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(OtherActor);
		IsValid(Product))
	{
		CargoProducts.Remove(Product);
	}
}
