#include "Features/Employees/CSStoreEmployeeCharacter.h"

#include "AIController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Data/CSStoreEmployeeRoleDataAsset.h"
#include "Features/Checkout/CSCheckoutCounterActor.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Delivery/CSDeliveryPortalActor.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Employees/CSStoreEmployeeAIController.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Storage/CSFreshnessSafeZoneActor.h"
#include "Systems/Employees/CSStoreEmployeeSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/TextRenderComponent.h"
#include "UI/Store/CSEmployeeHiringWidget.h"

namespace
{
bool TryGetDeliveryCrateApproachPoint(
	const ACSDeliveryCrateActor* Crate, float MaxPickupDistance, FVector& OutLocation)
{
	// Owner는 운반 후에도 배송 포털로 남으므로, 실제 입고 대기 위치도 확인합니다.
	if (!IsValid(Crate) || !Crate->IsAwaitingStockroomIntake() ||
		IsValid(Crate->GetAttachParentActor()))
	{
		return false;
	}

	const ACSDeliveryPortalActor* Portal = Cast<ACSDeliveryPortalActor>(Crate->GetOwner());
	return IsValid(Portal) && Portal->TryGetEmployeeDeliveryApproachPoint(OutLocation) &&
		FVector::DistSquared2D(Crate->GetActorLocation(), OutLocation) <= FMath::Square(MaxPickupDistance);
}
}

ACSStoreEmployeeCharacter::ACSStoreEmployeeCharacter()
{
	static ConstructorHelpers::FClassFinder<ACSDeliveryCrateActor> PackingClass(TEXT("/Game/ConvenienceStore/Blueprints/Delivery/BP_DeliveryCrateBase"));
	PackingCrateClass = PackingClass.Class;
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = ACSStoreEmployeeAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	HiringNameplate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HiringNameplate"));
	HiringNameplate->SetupAttachment(GetRootComponent());
	HiringNameplate->SetRelativeLocation(FVector(0.0f, 0.0f, 125.0f));
	HiringNameplate->SetHorizontalAlignment(EHTA_Center);
	HiringNameplate->SetWorldSize(20.0f);
	HiringNameplate->SetHiddenInGame(true);
	MagicBoostNameplate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("MagicBoostNameplate"));
	MagicBoostNameplate->SetupAttachment(GetRootComponent());
	MagicBoostNameplate->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	MagicBoostNameplate->SetHorizontalAlignment(EHTA_Center);
	MagicBoostNameplate->SetWorldSize(22.0f);
	MagicBoostNameplate->SetTextRenderColor(FColor::Cyan);
	MagicBoostNameplate->SetHiddenInGame(true);

	// 역할 에셋을 연결하지 않아도 신규 직원 블루프린트가 즉시 동작하도록 기본 업무를 모두 허용합니다.
	FallbackSupportedTaskTypes = {
		ECSStoreEmployeeTaskType::CheckoutService,
		ECSStoreEmployeeTaskType::RestockShelf,
		ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct,
	};

	// 손 소켓이 없는 리그에서도 상품을 보이게 하기 위해 스켈레탈 메시 아래에 별도 앵커를 둡니다.
	RightHandCarryAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RightHandCarryAnchor"));
	RightHandCarryAnchor->SetupAttachment(GetMesh(), RightHandSocketName);
	RightHandCarryAnchor->SetRelativeLocation(FVector::ZeroVector);

	// 상자는 한 액터를 몸통 앞에 한 번만 부착하고, 양손 위치는 애니메이션 또는 IK에서 상자 손잡이로 맞춥니다.
	CrateCarryAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("CrateCarryAnchor"));
	CrateCarryAnchor->SetupAttachment(GetRootComponent());
	CrateCarryAnchor->SetRelativeLocation(FVector(55.0f, 0.0f, 25.0f));
	InventoryInteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InventoryInteractionBox"));
	InventoryInteractionBox->SetupAttachment(GetRootComponent());
	InventoryInteractionBox->SetBoxExtent(FVector(30.0f, 35.0f, 25.0f));
	InventoryInteractionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InventoryInteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InventoryInteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InventoryInteractionBox->SetGenerateOverlapEvents(false);
	InventoryInteractionBox->SetCanEverAffectNavigation(false);

	// 하나의 상자는 한 앵커에만 부착하고, 양손 IK 목표점을 상자 양옆에 배치해 양손 운반 자세를 만듭니다.
	CrateLeftHandIKTarget = CreateDefaultSubobject<USceneComponent>(TEXT("CrateLeftHandIKTarget"));
	CrateLeftHandIKTarget->SetupAttachment(CrateCarryAnchor);
	CrateLeftHandIKTarget->SetRelativeLocation(FVector(0.0f, -30.0f, 0.0f));

	CrateRightHandIKTarget = CreateDefaultSubobject<USceneComponent>(TEXT("CrateRightHandIKTarget"));
	CrateRightHandIKTarget->SetupAttachment(CrateCarryAnchor);
	CrateRightHandIKTarget->SetRelativeLocation(FVector(0.0f, 30.0f, 0.0f));

	// 직원끼리는 통로가 좁아도 서로 밀거나 회피하다가 멈추지 않고 통과합니다.
	GetCharacterMovement()->bUseRVOAvoidance = false;
	// 직원·손님·플레이어는 Pawn 채널에서 서로를 막지 않고 통과합니다.
	// 월드 지오메트리, 진열대, 상자 등의 충돌 응답은 변경하지 않습니다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}

void ACSStoreEmployeeCharacter::ApplyMagicHit_Implementation(FVector HitLocation, FVector ImpactDirection, float KnockbackStrength)
{
	if (!HasAuthority() || !CanWork() || !CanHandleTask(ECSStoreEmployeeTaskType::RestockShelf)) return;
	if (!GetWorldTimerManager().IsTimerActive(MagicBoostTimer))
	{
		MoveSpeedBeforeBoost = GetCharacterMovement()->MaxWalkSpeed;
	}
	GetCharacterMovement()->MaxWalkSpeed = MoveSpeedBeforeBoost * FMath::Max(1.0f, MagicMoveMultiplier);
	WorkSpeedMultiplier = FMath::Max(1.0f, MagicWorkMultiplier);
	GetWorldTimerManager().SetTimer(MagicBoostTimer, this, &ThisClass::EndMagicBoost, FMath::Max(0.1f, MagicBoostDuration), false);
	MagicBoostNameplate->SetHiddenInGame(false);
	OnMagicBoostChanged(true);
}

float ACSStoreEmployeeCharacter::GetMagicBoostRemainingSeconds() const
{
	return FMath::Max(0.0f, GetWorldTimerManager().GetTimerRemaining(MagicBoostTimer));
}

void ACSStoreEmployeeCharacter::EndMagicBoost()
{
	GetCharacterMovement()->MaxWalkSpeed = MoveSpeedBeforeBoost;
	WorkSpeedMultiplier = 1.0f;
	MagicBoostNameplate->SetHiddenInGame(true);
	OnMagicBoostChanged(false);
}

void ACSStoreEmployeeCharacter::BeginPlay()
{
	Super::BeginPlay();
	// 블루프린트의 Pawn 통과 설정과 별개로 마법 투사체를 감지합니다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	RefreshEmploymentPresentation();

	if (UWorld* World = GetWorld())
	{
		if (UCSStoreEmployeeSubsystem* EmployeeSubsystem =
			World->GetSubsystem<UCSStoreEmployeeSubsystem>())
		{
			EmployeeSubsystem->RegisterEmployee(this);
		}
	}

	// 블루프린트에서 Auto Possess AI 설정이 바뀐 경우에도 직원이 멈추지 않도록 기본 AI 컨트롤러를 보장합니다.
	if (!IsValid(GetController()))
	{
		SpawnDefaultController();
	}

	if (IsValid(RightHandCarryAnchor) && IsValid(GetMesh()))
	{
		// BP에서 소켓 이름을 바꾼 경우에도 게임 시작 시 최신 오른손 소켓으로 다시 연결합니다.
		RightHandCarryAnchor->AttachToComponent(
			GetMesh(),
			FAttachmentTransformRules::KeepRelativeTransform,
			RightHandSocketName);
	}
}

void ACSStoreEmployeeCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(MagicBoostTimer);
	if (IsValid(ActiveHiringWidget)) ActiveHiringWidget->Close();
	CancelIdleReturn();
	RestoreWorkRotationSettings();

	if (UWorld* World = GetWorld())
	{
		if (UCSStoreEmployeeSubsystem* EmployeeSubsystem =
			World->GetSubsystem<UCSStoreEmployeeSubsystem>())
		{
			EmployeeSubsystem->UnregisterEmployee(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ACSStoreEmployeeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AActor* CarriedInventory = GetCarriedInventoryForPlayer();
	InventoryInteractionBox->SetCollisionEnabled(CarriedInventory ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (CarriedInventory) InventoryInteractionBox->SetWorldLocation(CarriedInventory->GetActorLocation());
	if (GetMagicBoostRemainingSeconds() > 0.0f)
	{
		// 기본 글꼴에서도 읽을 수 있도록 숫자와 영문으로 남은 시간을 표시합니다.
		MagicBoostNameplate->SetText(FText::FromString(FString::Printf(TEXT("BOOST %ds"), FMath::CeilToInt(GetMagicBoostRemainingSeconds()))));
	}
	// 지원자와 다음 날 출근 예정자는 고용소 자리를 지킵니다.
	if (!CanWork()) return;

	if (!bHasActiveTask)
	{
		NoTaskDiagnosticElapsedSeconds += DeltaSeconds;
		TryAcquireTask();
		if (!bHasActiveTask)
		{
			UpdateIdleBehavior(DeltaSeconds);
		}
		return;
	}

	UpdateActiveTask(DeltaSeconds);
}

bool ACSStoreEmployeeCharacter::CanHandleTask(ECSStoreEmployeeTaskType TaskType) const
{
	return GetSupportedTaskTypes().Contains(TaskType);
}

void ACSStoreEmployeeCharacter::RelinquishInventoryToPlayer(AActor* Inventory)
{
	if (HeldProduct == Inventory)
	{
		// 상자에서 꺼낸 임시 상품을 가져가는 경우 원본 상자 수량도 함께 차감합니다.
		if (bHeldProductWasSpawnedFromCrate && IsValid(HeldCrate)) HeldCrate->ConsumeOne();
		HeldProduct = nullptr;
		bHeldProductWasSpawnedFromCrate = false;
		bHasHeldProductReturnTransform = false;
	}
	if (HeldCrate == Inventory)
	{
		if (bHeldProductWasSpawnedFromCrate) ClearHeldProduct();
		HeldCrate = nullptr;
		bHasHeldCrateReturnLocation = false;
	}
	if (bHasActiveTask && (ActiveTask.SourceActor == Inventory || !IsValid(HeldCrate)))
	{
		// 넘겨준 액터를 분리한 뒤 중단해야 플레이어 손에서 재고가 반환되거나 삭제되지 않습니다.
		ClearActiveTask(false);
	}
}

AActor* ACSStoreEmployeeCharacter::GetCarriedInventoryForPlayer() const
{
	if (IsValid(HeldCrate)) return HeldCrate;
	return IsValid(HeldProduct) ? HeldProduct.Get() : nullptr;
}

bool ACSStoreEmployeeCharacter::IsCarryingCrate() const
{
	// 이 구현 파일에서는 배송 상자 클래스 정의를 포함하므로 UObject 유효성 검사를 안전하게 수행할 수 있습니다.
	return IsValid(HeldCrate);
}

bool ACSStoreEmployeeCharacter::IsHoldingProduct() const
{
	// 이 구현 파일에서는 상품 클래스 정의를 포함하므로 UObject 유효성 검사를 안전하게 수행할 수 있습니다.
	return IsValid(HeldProduct);
}

void ACSStoreEmployeeCharacter::SetIgnoreEmployeeMovement(
	ACSStoreEmployeeCharacter* OtherEmployee,
	bool bIgnore)
{
	if (!IsValid(OtherEmployee) || OtherEmployee == this)
	{
		return;
	}

	if (bIgnore)
	{
		MoveIgnoreActorAdd(OtherEmployee);
	}
	else
	{
		MoveIgnoreActorRemove(OtherEmployee);
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->IgnoreActorWhenMoving(OtherEmployee, bIgnore);
	}
}

const TArray<ECSStoreEmployeeTaskType>& ACSStoreEmployeeCharacter::GetSupportedTaskTypes() const
{
	// 계산원·진열원처럼 업무가 고정된 하위 클래스는 역할 에셋이 잘못 지정되어도 다른 업무를 수령하지 않습니다.
	if (bLockToFallbackSupportedTaskTypes)
	{
		return FallbackSupportedTaskTypes;
	}

	// 범용 직원은 역할 에셋이 지정되면 그 설정을 우선하여, 데이터만으로 새로운 역할을 구성할 수 있습니다.
	return IsValid(RoleData) ? RoleData->SupportedTaskTypes : FallbackSupportedTaskTypes;
}

void ACSStoreEmployeeCharacter::TryAcquireTask()
{
	UWorld* World = GetWorld();
	UCSStoreEmployeeSubsystem* EmployeeSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSStoreEmployeeSubsystem>() : nullptr;
	if (!IsValid(EmployeeSubsystem))
	{
		if (NoTaskDiagnosticElapsedSeconds >= 5.0f)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreEmployee] %s 직원이 업무를 수령하지 못했습니다: 직원 업무 서브시스템을 찾을 수 없습니다."),
				*GetName());
			NoTaskDiagnosticElapsedSeconds = 0.0f;
		}
		return;
	}

	FCSStoreEmployeeTask NewTask;
	const bool bShouldLogNoTaskReason = NoTaskDiagnosticElapsedSeconds >= 5.0f;
	FString NoTaskReason;
	if (!EmployeeSubsystem->TryClaimBestTask(
		this,
		GetSupportedTaskTypes(),
		NewTask,
		bShouldLogNoTaskReason ? &NoTaskReason : nullptr))
	{
		if (bShouldLogNoTaskReason)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[StoreEmployee] %s 직원이 업무를 수령하지 못했습니다: %s"),
				*GetName(),
				*NoTaskReason);
			NoTaskDiagnosticElapsedSeconds = 0.0f;
		}
		return;
	}

	// 대기 자리와 복귀 이동을 먼저 정리합니다. 계산 업무가 예약한 계산대는 업무 중에도 유지합니다.
	CancelIdleReturn(NewTask.Type != ECSStoreEmployeeTaskType::CheckoutService);
	ActiveTask = NewTask;
	bHasActiveTask = true;
	NoTaskDiagnosticElapsedSeconds = 0.0f;
	bHasReachedWorkTarget = false;
	bHasIssuedMoveRequest = false;
	bWaitingForEmployeeCheckout = false;
	WorkElapsedSeconds = 0.0f;
	MoveSettledElapsedSeconds = 0.0f;
	MoveRetryCount = 0;
	bReturnHeldCrateAsTaskSuccess = false;
	bHasAttemptedFailureStorageRecovery = false;
	FailureRecoveryStoragePoint = nullptr;
	RemainingRestockQuantity = ActiveTask.RequestedQuantity;
	RestockPhase = (ActiveTask.Type == ECSStoreEmployeeTaskType::RestockShelf ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::StoreCrate ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::StoreLooseProduct ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::ProtectPerishableStock)
		? ECSStoreEmployeeRestockPhase::MoveToSource
		: ECSStoreEmployeeRestockPhase::None;

	// 역할 설정과 실제 배정 결과를 확인할 수 있도록 업무 수령 사실을 로그에 남깁니다.
	UE_LOG(LogTemp, Log,
		TEXT("[StoreEmployee] %s 직원이 %d번 업무(%d)를 수령했습니다. 대상: %s, 재고 상자: %s, 요청 수량=%d"),
		*GetName(),
		ActiveTask.TaskId,
		static_cast<int32>(ActiveTask.Type),
		IsValid(ActiveTask.TargetActor) ? *ActiveTask.TargetActor->GetName() : TEXT("없음"),
		IsValid(ActiveTask.SourceActor) ? *ActiveTask.SourceActor->GetName() : TEXT("없음"),
		ActiveTask.RequestedQuantity);
}

void ACSStoreEmployeeCharacter::UpdateActiveTask(float DeltaSeconds)
{
	if (UCSStoreEmployeeSubsystem* EmployeeSubsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCSStoreEmployeeSubsystem>()
		: nullptr;
		!IsValid(EmployeeSubsystem) ||
		!EmployeeSubsystem->IsTaskAssignedToEmployee(ActiveTask.TaskId, this))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[StoreEmployee] %s 직원의 %d번 업무가 외부에서 취소되어 작업을 중단합니다."),
			*GetName(),
			ActiveTask.TaskId);
		ClearActiveTask(false);
		return;
	}

	FVector WorkLocation = FVector::ZeroVector;
	AActor* TargetActor = nullptr;
	if (!GetCurrentWorkLocation(WorkLocation, TargetActor))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreEmployee] %s 직원이 업무 대상의 이동 가능한 접근 지점을 찾지 못했습니다."),
			*GetName());
		ClearActiveTask(false);
		return;
	}

	// 내비게이션은 바닥 평면(XY)에서 도착 여부를 판정합니다. 포털 접근 지점은
	// NavMesh에 투영되므로 Z가 직원 캡슐 원점과 다를 수 있으며, 여기서 3D 거리를
	// 사용하면 AI는 이미 도착했다고 멈췄는데 직원 로직만 "아직 멀다"고 판단하여
	// MoveTo 요청을 반복하게 됩니다.
	const float DistanceSquared = FVector::DistSquared2D(
		GetActorLocation(), WorkLocation);
	const ACSDeliveryCrateActor* SourceDeliveryCrate =
		RestockPhase == ECSStoreEmployeeRestockPhase::MoveToSource
			? Cast<ACSDeliveryCrateActor>(ActiveTask.SourceActor)
			: nullptr;
	FVector DeliveryApproachLocation;
	const bool bIsDeliveryPortalSource = TryGetDeliveryCrateApproachPoint(
		SourceDeliveryCrate, DeliveryPortalPickupMaxDistance, DeliveryApproachLocation);
	// 배송 상자 더미에서는 접근 화살표 근처에서 멈춰야 300cm 집기 제한 안에 안정적으로 들어옵니다.
	float RequestedAcceptanceRadius = bIsDeliveryPortalSource
		? FMath::Min(WorkAcceptanceRadius, 75.0f)
		: WorkAcceptanceRadius;
	float RequestedArrivalTolerance = bIsDeliveryPortalSource
		? FMath::Min(WorkArrivalTolerance, 25.0f)
		: WorkArrivalTolerance;
	if (IsValid(SourceDeliveryCrate) &&
		Cast<ACSFreshnessSafeZoneActor>(SourceDeliveryCrate->GetAttachParentActor()) != nullptr)
	{
		// 접근 지점과 슬롯 사이 거리까지 포함해 실제 집기 범위 안에서 멈춥니다.
		const float PickupArrivalRadius = FMath::Max(1.0f, SourcePickupMaxDistance -
			FVector::Dist2D(WorkLocation, SourceDeliveryCrate->GetActorLocation()) - 1.0f);
		RequestedAcceptanceRadius = FMath::Min(RequestedAcceptanceRadius, PickupArrivalRadius);
		RequestedArrivalTolerance = FMath::Min(RequestedArrivalTolerance,
			FMath::Max(0.0f, PickupArrivalRadius - RequestedAcceptanceRadius));
		if (FVector::DistSquared2D(GetActorLocation(), SourceDeliveryCrate->GetActorLocation()) >
			FMath::Square(SourcePickupMaxDistance))
		{
			bHasReachedWorkTarget = false;
		}
	}
	if (Cast<ACSCheckoutCounterActor>(TargetActor) != nullptr)
	{
		// 기본 반경(160cm)으로는 직원 구역 밖에서 도착 처리될 수 있으므로, 구역 중심까지 충분히 접근합니다.
		RequestedAcceptanceRadius = FMath::Min(RequestedAcceptanceRadius, 50.0f);
		RequestedArrivalTolerance = FMath::Min(RequestedArrivalTolerance, 25.0f);
	}
	if (const ACSShelfActor* ShelfWorkTarget = Cast<ACSShelfActor>(TargetActor))
	{
		FVector UnusedCenter = FVector::ZeroVector;
		FRotator UnusedFacing = FRotator::ZeroRotator;
		float BrowseRadius = 0.0f;
		if (ShelfWorkTarget->TryGetBrowseZone(UnusedCenter, UnusedFacing, BrowseRadius))
		{
			// CustomerShoppingPoint를 공유하는 직원은 BrowseZoneBox 밖에서 업무를 시작하면 안 됩니다.
			// 기본 이동 반경(160cm)과 허용 오차(100cm)는 BrowseZone 반경 75cm보다 커서,
			// 진열대 옆·다른 진열대 앞에서 원격 작업이 시작되는 원인이었습니다.
			RequestedAcceptanceRadius = FMath::Min(RequestedAcceptanceRadius, BrowseRadius / 3.0f);
			RequestedArrivalTolerance = FMath::Min(
				RequestedArrivalTolerance,
				FMath::Max(0.0f, BrowseRadius - RequestedAcceptanceRadius));
		}
	}
	// 오배치 상품도 이제 상품 좌표가 아닌 해당 진열대의 CustomerShoppingPoint를 목표로 합니다.
	// 따라서 다른 진열대 앞에서 이미 도착했다고 판단하지 않도록 일반 도착 거리만 사용합니다.
	const float WorkStartDistance = RequestedAcceptanceRadius + RequestedArrivalTolerance;
	const AAIController* ExistingAIController = Cast<AAIController>(GetController());
	if (!bHasReachedWorkTarget && DistanceSquared > FMath::Square(WorkStartDistance))
	{
		RestoreWorkRotationSettings();

		if (bHasIssuedMoveRequest && IsValid(ExistingAIController) &&
			ExistingAIController->GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			// MoveToLocation 직후의 Idle 구간은 경로 준비나 AlreadyAtGoal 처리 중에도 발생할 수 있습니다.
			// 짧은 시간에 실패로 확정하면 배송 상자마다 요청-취소가 반복되므로 충분히 기다린 뒤에만 재시도합니다.
			MoveSettledElapsedSeconds += DeltaSeconds;
			if (MoveSettledElapsedSeconds >= 3.0f)
			{
				bHasIssuedMoveRequest = false;
				MoveSettledElapsedSeconds = 0.0f;
				++MoveRetryCount;
			}
		}
		else
		{
			MoveSettledElapsedSeconds = 0.0f;
		}

		AAIController* EmployeeAIController = Cast<AAIController>(GetController());
		if (!IsValid(EmployeeAIController))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[StoreEmployee] %s 직원에게 AI 컨트롤러가 없어 %s 대상으로 이동할 수 없습니다."),
				*GetName(), *TargetActor->GetName());
			ClearActiveTask(false);
			return;
		}

		if (!bHasIssuedMoveRequest)
		{
			if (MoveRetryCount >= 3)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[StoreEmployee] %s 직원의 %d번 업무 이동 실패: 대상=%s, 재시도 횟수=%d"),
					*GetName(), ActiveTask.TaskId, *TargetActor->GetName(), MoveRetryCount);
				ClearActiveTask(false);
				return;
			}

			// 같은 업무에서 매 프레임 새 이동 요청을 보내지 않고, 최초 요청의 성공·실패를 명확히 기록합니다.
			// 작업 도착 거리는 아래의 DistanceSquared 판정과 정확히 같아야 하므로,
			// 캡슐 반지름을 추가하는 StopOnOverlap 옵션은 사용하지 않습니다.
			const EPathFollowingRequestResult::Type MoveResult =
				EmployeeAIController->MoveToLocation(
					WorkLocation,
					RequestedAcceptanceRadius,
					false,
					true,
					true,
					true,
					nullptr,
					true);
			bHasIssuedMoveRequest = MoveResult != EPathFollowingRequestResult::Failed;

			if (bHasIssuedMoveRequest)
			{
				// UE_LOG의 로그 등급은 컴파일 시 고정되어야 하므로 성공과 실패를 분기해 기록합니다.
				UE_LOG(LogTemp, Log,
					TEXT("[StoreEmployee] %s 직원의 %s 접근 지점 이동 요청 결과: 성공, 목적지=(%.1f, %.1f, %.1f), 수평 거리=%.1fcm"),
					*GetName(), *TargetActor->GetName(),
					WorkLocation.X, WorkLocation.Y, WorkLocation.Z,
					FMath::Sqrt(DistanceSquared));
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[StoreEmployee] %s 직원의 %s 접근 지점 이동 요청 결과: 실패"),
					*GetName(), *TargetActor->GetName());
			}

			if (!bHasIssuedMoveRequest)
			{
				++MoveRetryCount;
			}
		}
		return;
	}

	if (!bHasReachedWorkTarget)
	{
		bHasReachedWorkTarget = true;
		if (AAIController* EmployeeAIController = Cast<AAIController>(GetController()))
		{
			EmployeeAIController->StopMovement();
		}
	}

	// 도착 시 남아 있는 이동 방향을 작업 방향으로 바꾸고, 작업 중에도 같은 방향을 유지합니다.
	if (!UpdateWorkFacing(TargetActor, DeltaSeconds))
	{
		return;
	}

	if (ActiveTask.Type == ECSStoreEmployeeTaskType::CheckoutService)
	{
		ACSCheckoutCounterActor* Counter = Cast<ACSCheckoutCounterActor>(ActiveTask.TargetActor);
		if (!IsValid(Counter))
		{
			ClearActiveTask(false);
			return;
		}

		// NavMesh 거리만으로 도착 처리하지 않고, 실제 CashierInteractionZone 안에서만 계산합니다.
		if (!Counter->IsInCashierInteractionZone(this))
		{
			RestoreWorkRotationSettings();
			bHasReachedWorkTarget = false;
			bHasIssuedMoveRequest = false;
			MoveSettledElapsedSeconds = 0.0f;
			return;
		}

		if (!bWaitingForEmployeeCheckout)
		{
			const float WorkDuration = IsValid(RoleData) ? RoleData->WorkDurationSeconds : 1.5f;
			// 이동 판정과 대기 시간은 유지하고 실제 작업 진행량만 배속합니다.
			WorkElapsedSeconds += DeltaSeconds * WorkSpeedMultiplier;
			if (WorkElapsedSeconds < WorkDuration)
			{
				return;
			}

			if (!Counter->TryProcessEmployeeCheckout())
			{
				ClearActiveTask(false);
				return;
			}

			bWaitingForEmployeeCheckout = true;
			return;
		}

		if (Counter->IsEmployeeCheckoutInProgress())
		{
			return;
		}

		const bool bSucceeded = Counter->DidLastEmployeeCheckoutSucceed();
		bWaitingForEmployeeCheckout = false;
		ClearActiveTask(bSucceeded);
		return;
	}

	if (ActiveTask.Type == ECSStoreEmployeeTaskType::RestockShelf)
	{
		UpdateRestockTask(DeltaSeconds);
		return;
	}
	if (ActiveTask.Type == ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct)
	{
		if (Cast<ACSShelfActor>(ActiveTask.TargetActor) != nullptr)
		{
			UpdateRestockTask(DeltaSeconds);
		}
		else
		{
			UpdateStorageTask(DeltaSeconds);
		}
		return;
	}
	if (ActiveTask.Type == ECSStoreEmployeeTaskType::ProtectPerishableStock)
	{
		if (Cast<ACSShelfActor>(ActiveTask.TargetActor) != nullptr)
		{
			UpdateRestockTask(DeltaSeconds);
		}
		else
		{
			UpdateStorageTask(DeltaSeconds);
		}
		return;
	}
	if (ActiveTask.Type == ECSStoreEmployeeTaskType::StoreCrate ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::StoreLooseProduct)
	{
		UpdateStorageTask(DeltaSeconds);
		return;
	}

	const float WorkDuration = IsValid(RoleData) ? RoleData->WorkDurationSeconds : 1.5f;
	// 이동 판정과 대기 시간은 유지하고 실제 작업 진행량만 배속합니다.
	WorkElapsedSeconds += DeltaSeconds * WorkSpeedMultiplier;
	if (WorkElapsedSeconds >= WorkDuration)
	{
		const bool bSucceeded = ExecuteActiveTask();
		if (bSucceeded)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[StoreEmployee] %s 직원의 %d번 업무 처리 결과: 성공"),
				*GetName(), ActiveTask.TaskId);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreEmployee] %s 직원의 %d번 업무 처리 결과: 실패"),
				*GetName(), ActiveTask.TaskId);
		}
		ClearActiveTask(bSucceeded);
	}
}

void ACSStoreEmployeeCharacter::UpdateRestockTask(float DeltaSeconds)
{
	// 이동 판정과 대기 시간은 유지하고 실제 작업 진행량만 배속합니다.
	WorkElapsedSeconds += DeltaSeconds * WorkSpeedMultiplier;

	switch (RestockPhase)
	{
	case ECSStoreEmployeeRestockPhase::MoveToSource:
		if (WorkElapsedSeconds >= CratePickupDurationSeconds && PickUpSourceCrate())
		{
			BeginRestockPhase(ECSStoreEmployeeRestockPhase::MoveToShelf);
		}
		else if (WorkElapsedSeconds >= CratePickupDurationSeconds)
		{
			ClearActiveTask(false);
		}
		break;

	case ECSStoreEmployeeRestockPhase::MoveToShelf:
		// 상자를 든 채 진열대에 도착하면, 상품을 오른손으로 옮기지 않고 상자에서 바로 슬롯에 배치합니다.
		// 바닥 상품 업무만 기존처럼 오른손에 든 상품을 배치합니다.
		BeginRestockPhase((IsValid(HeldCrate) || IsValid(HeldProduct))
			? ECSStoreEmployeeRestockPhase::PlaceProductOnShelf
			: ECSStoreEmployeeRestockPhase::None);
		break;

	case ECSStoreEmployeeRestockPhase::MoveToRecoveryStorage:
	{
		const bool bStoredInRecoveryStorage = StoreHeldInventoryInRecoveryStorage();
		if (bStoredInRecoveryStorage)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[StoreEmployee] %s 직원이 %d번 반납 재고를 창고에 안전하게 보관했습니다."),
				*GetName(), ActiveTask.TaskId);
		}
		const bool bSucceeded = bReturnHeldCrateAsTaskSuccess;
		if (!bStoredInRecoveryStorage && BeginHeldInventoryReturn(bSucceeded))
		{
			return;
		}
		bReturnHeldCrateAsTaskSuccess = false;
		ClearActiveTask(bStoredInRecoveryStorage ? bSucceeded : false);
		break;
	}

	case ECSStoreEmployeeRestockPhase::TakeProductFromCrate:
		if (WorkElapsedSeconds >= ProductPickupDurationSeconds)
		{
			if (TakeProductFromHeldCrate())
			{
				BeginRestockPhase(ECSStoreEmployeeRestockPhase::PlaceProductOnShelf);
			}
			else
			{
				ClearActiveTask(false);
			}
		}
		break;

	case ECSStoreEmployeeRestockPhase::PlaceProductOnShelf:
		if (WorkElapsedSeconds >= ProductPlacementDurationSeconds)
		{
			if (!PlaceHeldProductOnShelf())
			{
				ClearActiveTask(false);
				return;
			}

			--RemainingRestockQuantity;
			ACSShelfActor* Shelf = Cast<ACSShelfActor>(ActiveTask.TargetActor);
			// 기존 단을 채운 뒤 같은 진열대의 미지정 빈 단까지 이어서 채웁니다.
			if (ActiveTask.Type == ECSStoreEmployeeTaskType::RestockShelf && IsValid(HeldCrate) &&
				HeldCrate->GetRemainingQuantity() > 0 && IsValid(Shelf) &&
				(RemainingRestockQuantity <= 0 || !Shelf->CanEmployeeRestockProduct(
					ActiveTask.ProductRow, ActiveTask.bAllowCrossTierDuplicateFallback)))
			{
				ActiveTask.bAllowCrossTierDuplicateFallback = true;
				RemainingRestockQuantity = FMath::Min(HeldCrate->GetRemainingQuantity(),
					Shelf->GetEmployeeRestockableSlotCount(ActiveTask.ProductRow, true));
			}
			UE_LOG(LogTemp, Log,
				TEXT("[StoreEmployee] %s 직원이 %d번 진열 업무에서 상품 1개를 배치했습니다. 남은 요청 수량=%d, 상자 잔여 수량=%d"),
				*GetName(),
				ActiveTask.TaskId,
				RemainingRestockQuantity,
				IsValid(HeldCrate) ? HeldCrate->GetRemainingQuantity() : 0);
			if (RemainingRestockQuantity > 0 && IsValid(HeldCrate) &&
				HeldCrate->GetRemainingQuantity() > 0 && IsValid(Shelf) &&
				Shelf->CanEmployeeRestockProduct(
					ActiveTask.ProductRow,
					ActiveTask.bAllowCrossTierDuplicateFallback))
			{
				// 상자는 계속 운반 앵커에 붙어 있으므로, 다음 상품도 같은 위치에서 바로 진열합니다.
				BeginRestockPhase(ECSStoreEmployeeRestockPhase::PlaceProductOnShelf);
			}
			else
			{
				const bool bHasRemainingCrate = IsValid(HeldCrate) && HeldCrate->GetRemainingQuantity() > 0;
				if (bHasRemainingCrate)
				{
					// 진열대가 가득 차도 상자를 즉시 순간이동시키지 않습니다.
					// 다른 종료 경로와 동일하게 적합 창고를 먼저 찾고, 없으면 원래 위치로 직접 반납합니다.
					if (BeginHeldInventoryReturn(true))
					{
						return;
					}
				}

				ReleaseHeldCrate();
				UE_LOG(LogTemp, Log,
					TEXT("[StoreEmployee] %s 직원의 %d번 진열 업무 처리 결과: 성공"),
					*GetName(), ActiveTask.TaskId);
				ClearActiveTask(true);
			}
		}
		break;

	case ECSStoreEmployeeRestockPhase::MoveToReturnCrate:
		// 취소·남은 재고 모두 상자를 직접 반환한 뒤에만 내려놓습니다.
		{
			const bool bSucceeded = bReturnHeldCrateAsTaskSuccess;
			bReturnHeldCrateAsTaskSuccess = false;
		ReleaseHeldCrate();
		ClearActiveTask(bSucceeded);
		}
		break;

	case ECSStoreEmployeeRestockPhase::MoveToReturnProduct:
		ReleaseHeldProductToReturnLocation();
		ClearActiveTask(false);
		break;

	default:
		ClearActiveTask(false);
		break;
	}
}

void ACSStoreEmployeeCharacter::UpdateStorageTask(float DeltaSeconds)
{
	// 이동 판정과 대기 시간은 유지하고 실제 작업 진행량만 배속합니다.
	WorkElapsedSeconds += DeltaSeconds * WorkSpeedMultiplier;

	switch (RestockPhase)
	{
	case ECSStoreEmployeeRestockPhase::MoveToSource:
		if (WorkElapsedSeconds >= CratePickupDurationSeconds && PickUpSourceCrate())
		{
			// 상자를 든 뒤에는 같은 이동 단계를 재사용해 창고 접근 지점으로 향합니다.
			BeginRestockPhase(ECSStoreEmployeeRestockPhase::MoveToShelf);
		}
		else if (WorkElapsedSeconds >= CratePickupDurationSeconds)
		{
			ClearActiveTask(false);
		}
		break;

	case ECSStoreEmployeeRestockPhase::MoveToShelf:
	{
		ACSFreshnessSafeZoneActor* StoragePoint =
			Cast<ACSFreshnessSafeZoneActor>(ActiveTask.TargetActor);
		const bool bStoredCrate = IsValid(HeldCrate) && StoragePoint != nullptr &&
			StoragePoint->TryStoreCrate(HeldCrate, true);
		const bool bStoredLooseProduct = IsValid(HeldProduct) && StoragePoint != nullptr &&
			StoragePoint->TryStoreLooseProduct(HeldProduct);
		if (!bStoredCrate && !bStoredLooseProduct)
		{
			ClearActiveTask(false);
			return;
		}

		// 창고가 상자 또는 상품의 소유·부착 상태를 넘겨받았으므로 직원 정리 단계에서 다시 떨어뜨리지 않습니다.
		HeldCrate = nullptr;
		HeldProduct = nullptr;
		bHeldProductWasSpawnedFromCrate = false;
		UE_LOG(LogTemp, Log,
			TEXT("[StoreEmployee] %s 직원이 %d번 창고 보관 업무를 완료했습니다."),
			*GetName(), ActiveTask.TaskId);
		ClearActiveTask(true);
		break;
	}

	case ECSStoreEmployeeRestockPhase::MoveToRecoveryStorage:
	{
		const bool bStoredInRecoveryStorage = StoreHeldInventoryInRecoveryStorage();
		if (bStoredInRecoveryStorage)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[StoreEmployee] %s 직원이 %d번 반납 재고를 창고에 안전하게 보관했습니다."),
				*GetName(), ActiveTask.TaskId);
		}
		const bool bSucceeded = bReturnHeldCrateAsTaskSuccess;
		if (!bStoredInRecoveryStorage && BeginHeldInventoryReturn(bSucceeded))
		{
			return;
		}
		bReturnHeldCrateAsTaskSuccess = false;
		ClearActiveTask(bStoredInRecoveryStorage ? bSucceeded : false);
		break;
	}

	case ECSStoreEmployeeRestockPhase::MoveToReturnCrate:
		// 창고 보관이 실패해도 상자를 들고 출발 지점으로 돌아간 뒤에 업무를 종료합니다.
		ReleaseHeldCrate();
		ClearActiveTask(false);
		break;

	case ECSStoreEmployeeRestockPhase::MoveToReturnProduct:
		ReleaseHeldProductToReturnLocation();
		ClearActiveTask(false);
		break;

	default:
		ClearActiveTask(false);
		break;
	}
}

bool ACSStoreEmployeeCharacter::ExecuteActiveTask()
{
	if (ActiveTask.Type == ECSStoreEmployeeTaskType::CheckoutService)
	{
		if (ACSCheckoutCounterActor* Counter = Cast<ACSCheckoutCounterActor>(ActiveTask.TargetActor))
		{
			return Counter->TryProcessEmployeeCheckout();
		}
	}
	else if (ActiveTask.Type == ECSStoreEmployeeTaskType::RestockShelf)
	{
		ACSShelfActor* Shelf = Cast<ACSShelfActor>(ActiveTask.TargetActor);
		ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(ActiveTask.SourceActor);
		if (IsValid(Shelf) && IsValid(Crate))
		{
			return Shelf->TryRestockFromCrate(Crate, ActiveTask.RequestedQuantity) > 0;
		}
	}

	return false;
}

void ACSStoreEmployeeCharacter::ClearActiveTask(bool bSucceeded)
{
	RestoreWorkRotationSettings();

	if (!bSucceeded && BeginHeldInventoryReturn(false))
	{
		return;
	}

	// 업무가 중단되어도 손에 붙은 임시 상품이나 상자가 월드에 남지 않도록 먼저 정리합니다.
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
	ClearHeldProduct();
	ReleaseHeldCrate();

	if (UWorld* World = GetWorld())
	{
		if (UCSStoreEmployeeSubsystem* EmployeeSubsystem =
			World->GetSubsystem<UCSStoreEmployeeSubsystem>())
		{
			EmployeeSubsystem->FinishTask(ActiveTask.TaskId, this, bSucceeded);
		}
	}

	ActiveTask = FCSStoreEmployeeTask();
	bHasActiveTask = false;
	bHasReachedWorkTarget = false;
	bHasIssuedMoveRequest = false;
	bWaitingForEmployeeCheckout = false;
	WorkElapsedSeconds = 0.0f;
	MoveSettledElapsedSeconds = 0.0f;
	MoveRetryCount = 0;
	bReturnHeldCrateAsTaskSuccess = false;
	bHasAttemptedFailureStorageRecovery = false;
	bHasHeldProductReturnTransform = false;
	FailureRecoveryStoragePoint = nullptr;
	RestockPhase = ECSStoreEmployeeRestockPhase::None;
	RemainingRestockQuantity = 0;
	IdleElapsedSeconds = 0.0f;
	IdleSearchCountdown = 0.0f;
}

bool ACSStoreEmployeeCharacter::BeginHeldInventoryReturn(bool bTaskSuccessAfterReturn)
{
	// 창고 보관 또는 원위치 반납 뒤 원래 업무를 성공 처리할지 결정합니다.
	// 진열대 포화만 성공 처리하고, 실패·취소는 재고를 안전하게 반납해도 실패로 기록합니다.
	bReturnHeldCrateAsTaskSuccess = bTaskSuccessAfterReturn;

	if (!bHasAttemptedFailureStorageRecovery)
	{
		bHasAttemptedFailureStorageRecovery = true;
		if (BeginFailureStorageRecovery())
		{
			return true;
		}
	}

	if (BeginHeldCrateReturn())
	{
		// 상자에서 임시로 생성한 낱개 상품은 상자 수량과 맞춰 제거합니다.
		ClearHeldProduct();
		return true;
	}

	return BeginHeldProductReturn();
}

bool ACSStoreEmployeeCharacter::GetCurrentWorkLocation(
	FVector& OutWorkLocation,
	AActor*& OutTargetActor) const
{
	OutWorkLocation = FVector::ZeroVector;
	OutTargetActor = ActiveTask.TargetActor;

	if (RestockPhase == ECSStoreEmployeeRestockPhase::MoveToRecoveryStorage)
	{
		OutTargetActor = FailureRecoveryStoragePoint;
		return IsValid(FailureRecoveryStoragePoint) &&
			FailureRecoveryStoragePoint->TryGetStorageApproachPoint(OutWorkLocation);
	}

	if (RestockPhase == ECSStoreEmployeeRestockPhase::MoveToReturnProduct)
	{
		if (!IsValid(HeldProduct) || !bHasHeldProductReturnTransform)
		{
			return false;
		}

		OutTargetActor = HeldProduct;
		const UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(GetWorld());
		FNavLocation ProjectedLocation;
		if (NavigationSystem == nullptr || !NavigationSystem->ProjectPointToNavigation(
			HeldProductReturnTransform.GetLocation(), ProjectedLocation, FVector(300.0f, 300.0f, 500.0f)))
		{
			return false;
		}

		OutWorkLocation = ProjectedLocation.Location;
		return true;
	}

	if (!IsValid(OutTargetActor))
	{
		return false;
	}

	if (RestockPhase == ECSStoreEmployeeRestockPhase::MoveToReturnCrate)
	{
		if (!IsValid(HeldCrate) || !bHasHeldCrateReturnLocation)
		{
			return false;
		}

		OutTargetActor = HeldCrate;
		const UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(GetWorld());
		FNavLocation ProjectedLocation;
		if (NavigationSystem == nullptr || !NavigationSystem->ProjectPointToNavigation(
			HeldCrateReturnLocation,
			ProjectedLocation,
			FVector(300.0f, 300.0f, 500.0f)))
		{
			return false;
		}

		OutWorkLocation = ProjectedLocation.Location;
		return true;
	}

	if ((ActiveTask.Type == ECSStoreEmployeeTaskType::RestockShelf ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::StoreCrate ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::StoreLooseProduct ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::ProtectPerishableStock) &&
		RestockPhase == ECSStoreEmployeeRestockPhase::MoveToSource)
	{
		OutTargetActor = ActiveTask.SourceActor;
		if (!IsValid(OutTargetActor))
		{
			return false;
		}

		const UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(GetWorld());
		FNavLocation ProjectedLocation;
		if (NavigationSystem == nullptr)
		{
			return false;
		}

		if (const ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(OutTargetActor))
		{
			if (const ACSFreshnessSafeZoneActor* StoragePoint =
				Cast<ACSFreshnessSafeZoneActor>(DeliveryCrate->GetAttachParentActor()))
			{
				// 공용 접근 지점에서 먼 슬롯은 해당 상자 근처의 NavMesh 위치로 접근합니다.
				const float MaxApproachDistance = FMath::Max(0.0f, SourcePickupMaxDistance - 2.0f);
				if (StoragePoint->TryGetStorageApproachPoint(OutWorkLocation) &&
					FVector::DistSquared2D(OutWorkLocation, DeliveryCrate->GetActorLocation()) <=
						FMath::Square(MaxApproachDistance))
				{
					return true;
				}
				if (NavigationSystem->ProjectPointToNavigation(DeliveryCrate->GetActorLocation(),
					ProjectedLocation, FVector(600.0f, 600.0f, 800.0f)) &&
					FVector::DistSquared2D(ProjectedLocation.Location, DeliveryCrate->GetActorLocation()) <=
						FMath::Square(MaxApproachDistance))
				{
					OutWorkLocation = ProjectedLocation.Location;
					return true;
				}
				return false;
			}
			if (TryGetDeliveryCrateApproachPoint(
				DeliveryCrate, DeliveryPortalPickupMaxDistance, OutWorkLocation))
			{
				return true;
			}
		}

		if (const ACSProductPickupActor* SourceProduct = Cast<ACSProductPickupActor>(OutTargetActor))
		{
			if (const ACSShelfActor* SourceShelf =
				Cast<ACSShelfActor>(SourceProduct->GetAttachParentActor()))
			{
				// 진열대에 있던 상품은 개별 슬롯 좌표로 이동하지 않습니다.
				// 손님과 공유하는 접근 마커로 이동해야 다른 진열대 앞에서 원격 집기하는 일이 없습니다.
				OutTargetActor = const_cast<ACSShelfActor*>(SourceShelf);
				return SourceShelf->TryGetCustomerShoppingPoint(OutWorkLocation);
			}
		}

		const FVector SourceLocation = OutTargetActor->GetActorLocation();
		if (NavigationSystem->ProjectPointToNavigation(
			SourceLocation,
			ProjectedLocation,
			FVector(600.0f, 600.0f, 800.0f)))
		{
			// 바닥·상단 진열·창고 슬롯처럼 원본 위치가 NavMesh 위가 아니어도 넓은 범위에서 가장 가까운 통로를 찾습니다.
			OutWorkLocation = ProjectedLocation.Location;
			return true;
		}

		if (NavigationSystem->GetRandomReachablePointInRadius(
			SourceLocation, 350.0f, ProjectedLocation))
		{
			// 벽 모서리나 물리 충돌 안에 떨어진 상품은 가까운 도달 가능 지점으로 이동한 뒤 집기 단계에서 처리합니다.
			OutWorkLocation = ProjectedLocation.Location;
			return true;
		}

		return false;
	}

	if (const ACSShelfActor* Shelf = Cast<ACSShelfActor>(OutTargetActor))
	{
		// 직원도 손님과 같은 CustomerShoppingPoint로 이동합니다.
		// 이 위치는 BrowseZoneBox의 건축 배치 검증에 포함되므로 별도 직원 작업 공간이 필요하지 않습니다.
		return Shelf->TryGetCustomerShoppingPoint(OutWorkLocation);
	}

	if (const ACSCheckoutCounterActor* Counter = Cast<ACSCheckoutCounterActor>(OutTargetActor))
	{
		// 대기 중 확보한 구역 내부 자리에서 바로 계산합니다. 업무 시작 때 막힌 중심으로 다시 이동하지 않습니다.
		FVector InteractionPoint;
		if (!Counter->TryGetCashierInteractionPoint(InteractionPoint))
		{
			return false;
		}
		if (Counter->IsInCashierInteractionZone(this))
		{
			OutWorkLocation = GetNavAgentLocation();
			return true;
		}
		// 계산 직원은 액터 원점(대기열 근처)이 아니라 계산대 뒷편 직원 상호작용 구역으로 이동합니다.
		OutWorkLocation = InteractionPoint;
		return true;
	}

	if (const ACSFreshnessSafeZoneActor* StoragePoint =
		Cast<ACSFreshnessSafeZoneActor>(OutTargetActor))
	{
		// 창고 포인트도 진열대와 마찬가지로 루트가 아닌 별도 접근 마커 위치로 이동합니다.
		return StoragePoint->TryGetStorageApproachPoint(OutWorkLocation);
	}

	// 전용 접근 마커가 없는 업무 대상은 액터 위치를 기본 작업 위치로 사용합니다.
	OutWorkLocation = OutTargetActor->GetActorLocation();
	return true;
}

bool ACSStoreEmployeeCharacter::TryGetCurrentWorkFacing(
	const AActor* TargetActor, FRotator& OutFacing) const
{
	OutFacing = FRotator::ZeroRotator;
	if (!IsValid(TargetActor))
	{
		return false;
	}

	// 진열대 원점이 한쪽에 치우쳐 있어도, BP의 접근 화살표가 지정한 정면을 바라봅니다.
	if (const ACSShelfActor* Shelf = Cast<ACSShelfActor>(TargetActor))
	{
		FVector UnusedCenter = FVector::ZeroVector;
		float UnusedRadius = 0.0f;
		if (Shelf->TryGetBrowseZone(UnusedCenter, OutFacing, UnusedRadius))
		{
			return true;
		}
	}

	if (const ACSCheckoutCounterActor* Counter = Cast<ACSCheckoutCounterActor>(TargetActor))
	{
		return Counter->TryGetCashierInteractionFacing(GetActorLocation(), OutFacing);
	}

	FVector FacingLocation = TargetActor->GetActorLocation();
	if (RestockPhase == ECSStoreEmployeeRestockPhase::MoveToReturnCrate && bHasHeldCrateReturnLocation)
	{
		// 손에 붙어 함께 회전하는 물건이 아니라, 실제로 내려놓을 원래 위치를 봅니다.
		FacingLocation = HeldCrateReturnLocation;
	}
	else if (RestockPhase == ECSStoreEmployeeRestockPhase::MoveToReturnProduct && bHasHeldProductReturnTransform)
	{
		FacingLocation = HeldProductReturnTransform.GetLocation();
	}

	FVector ToTarget = FacingLocation - GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.IsNearlyZero())
	{
		// 발밑의 상품처럼 수평 방향을 만들 수 없는 경우에는 현재 방향으로 작업합니다.
		return false;
	}

	OutFacing = ToTarget.Rotation();
	return true;
}

bool ACSStoreEmployeeCharacter::UpdateWorkFacing(const AActor* TargetActor, float DeltaSeconds)
{
	FRotator TargetFacing;
	if (!TryGetCurrentWorkFacing(TargetActor, TargetFacing))
	{
		return true;
	}
	return UpdateFacingRotation(TargetFacing, DeltaSeconds);
}

bool ACSStoreEmployeeCharacter::UpdateFacingRotation(const FRotator& TargetFacing, float DeltaSeconds)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!bHasWorkRotationOverride)
	{
		bSavedUseControllerRotationYaw = bUseControllerRotationYaw;
		bSavedOrientRotationToMovement = Movement->bOrientRotationToMovement;
		bSavedUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
		bHasWorkRotationOverride = true;
		// 이동 잔여 속도와 BP의 회전 설정이 작업 방향을 덮어쓰지 않게 합니다.
		Movement->StopMovementImmediately();
		bUseControllerRotationYaw = false;
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
	}

	FRotator NewRotation = FMath::RInterpConstantTo(
		FRotator(0.0f, GetActorRotation().Yaw, 0.0f), TargetFacing,
		DeltaSeconds, FMath::Max(1.0f, WorkFacingTurnRate));
	const bool bIsFacingTarget = FMath::Abs(
		FMath::FindDeltaAngleDegrees(NewRotation.Yaw, TargetFacing.Yaw)) <= 2.0f;
	if (bIsFacingTarget)
	{
		NewRotation = TargetFacing;
	}
	SetActorRotation(NewRotation);
	if (AController* EmployeeController = GetController())
	{
		EmployeeController->SetControlRotation(NewRotation);
	}
	return bIsFacingTarget;
}

void ACSStoreEmployeeCharacter::RestoreWorkRotationSettings()
{
	if (!bHasWorkRotationOverride)
	{
		return;
	}

	bUseControllerRotationYaw = bSavedUseControllerRotationYaw;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = bSavedOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;
	}
	bHasWorkRotationOverride = false;
}

void ACSStoreEmployeeCharacter::BeginRestockPhase(ECSStoreEmployeeRestockPhase NewPhase)
{
	RestockPhase = NewPhase;
	WorkElapsedSeconds = 0.0f;

	const bool bRequiresMovement = NewPhase == ECSStoreEmployeeRestockPhase::MoveToSource ||
		NewPhase == ECSStoreEmployeeRestockPhase::MoveToShelf ||
		NewPhase == ECSStoreEmployeeRestockPhase::MoveToRecoveryStorage ||
		NewPhase == ECSStoreEmployeeRestockPhase::MoveToReturnCrate ||
		NewPhase == ECSStoreEmployeeRestockPhase::MoveToReturnProduct;
	if (bRequiresMovement)
	{
		RestoreWorkRotationSettings();
		// 새 목적지로 이동할 때는 이전 단계의 도착·이동 요청 상태를 반드시 초기화합니다.
		bHasReachedWorkTarget = false;
		bHasIssuedMoveRequest = false;
		MoveSettledElapsedSeconds = 0.0f;
		MoveRetryCount = 0;
	}
}

bool ACSStoreEmployeeCharacter::BeginFailureStorageRecovery()
{
	ACSFreshnessSafeZoneActor* RecoveryStorage = nullptr;
	if (!FindFailureRecoveryStorage(RecoveryStorage))
	{
		return false;
	}

	FailureRecoveryStoragePoint = RecoveryStorage;
	BeginRestockPhase(ECSStoreEmployeeRestockPhase::MoveToRecoveryStorage);
	UE_LOG(LogTemp, Log,
		TEXT("[StoreEmployee] %s 직원이 %d번 %s 재고를 적합 창고로 회수합니다: 창고=%s"),
		*GetName(), ActiveTask.TaskId,
		bReturnHeldCrateAsTaskSuccess ? TEXT("진열 후 남은") : TEXT("실패 업무"),
		*RecoveryStorage->GetName());
	return true;
}

bool ACSStoreEmployeeCharacter::FindFailureRecoveryStorage(
	ACSFreshnessSafeZoneActor*& OutStoragePoint) const
{
	OutStoragePoint = nullptr;
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	// 상자에서 임시 생성된 상품은 수량이 아직 상자에 남아 있으므로 상자 자체를 우선 회수합니다.
	const bool bRecoverCrate = IsValid(HeldCrate);
	const bool bRecoverLooseProduct = !bRecoverCrate && IsValid(HeldProduct) && !bHeldProductWasSpawnedFromCrate;
	if (!bRecoverCrate && !bRecoverLooseProduct)
	{
		return false;
	}

	float NearestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<ACSFreshnessSafeZoneActor> StorageIt(World); StorageIt; ++StorageIt)
	{
		ACSFreshnessSafeZoneActor* StoragePoint = *StorageIt;
		FVector ApproachPoint;
		if (!IsValid(StoragePoint) || !StoragePoint->TryGetStorageApproachPoint(ApproachPoint) ||
			(bRecoverCrate && !StoragePoint->CanStoreCrate(HeldCrate, true)) ||
			(bRecoverLooseProduct && !StoragePoint->CanStoreLooseProduct(HeldProduct)))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(GetActorLocation(), ApproachPoint);
		const bool bHasSlot = bRecoverCrate && StoragePoint->CanStoreCrate(HeldCrate);
		const bool bBestHasSlot = bRecoverCrate && IsValid(OutStoragePoint) &&
			OutStoragePoint->CanStoreCrate(HeldCrate);
		if (!IsValid(OutStoragePoint) || (bHasSlot && !bBestHasSlot) ||
			(bHasSlot == bBestHasSlot && DistanceSquared < NearestDistanceSquared))
		{
			NearestDistanceSquared = DistanceSquared;
			OutStoragePoint = StoragePoint;
		}
	}

	return IsValid(OutStoragePoint);
}

bool ACSStoreEmployeeCharacter::StoreHeldInventoryInRecoveryStorage()
{
	ACSFreshnessSafeZoneActor* StoragePoint = FailureRecoveryStoragePoint;
	if (!IsValid(StoragePoint))
	{
		return false;
	}

	if (IsValid(HeldCrate) && StoragePoint->TryStoreCrate(HeldCrate, true))
	{
		HeldCrate = nullptr;
		bHasHeldCrateReturnLocation = false;
		return true;
	}

	if (IsValid(HeldProduct) && !bHeldProductWasSpawnedFromCrate &&
		StoragePoint->TryStoreLooseProduct(HeldProduct))
	{
		HeldProduct = nullptr;
		bHasHeldProductReturnTransform = false;
		return true;
	}

	return false;
}

bool ACSStoreEmployeeCharacter::BeginHeldCrateReturn()
{
	if (!IsValid(HeldCrate) || !bHasHeldCrateReturnLocation ||
		RestockPhase == ECSStoreEmployeeRestockPhase::MoveToReturnCrate)
	{
		return false;
	}

	FVector SafeReturnLocation = FVector::ZeroVector;
	if (FindSafeCrateReturnLocation(SafeReturnLocation))
	{
		// 원래 위치와 겹치는 경우에 대비해, 실제 반환 전에는 주변의 비어 있는 후보 위치로 바꿉니다.
		HeldCrateReturnLocation = SafeReturnLocation;
	}

	// 업무는 유지한 채 이동 단계만 바꾸어, 상자를 안전하게 돌려놓은 뒤 실패 완료 처리합니다.
	BeginRestockPhase(ECSStoreEmployeeRestockPhase::MoveToReturnCrate);
	return true;
}

bool ACSStoreEmployeeCharacter::BeginHeldProductReturn()
{
	if (!IsValid(HeldProduct) || bHeldProductWasSpawnedFromCrate ||
		!bHasHeldProductReturnTransform ||
		RestockPhase == ECSStoreEmployeeRestockPhase::MoveToReturnProduct)
	{
		return false;
	}

	BeginRestockPhase(ECSStoreEmployeeRestockPhase::MoveToReturnProduct);
	return true;
}

bool ACSStoreEmployeeCharacter::FindSafeCrateReturnLocation(FVector& OutReturnLocation) const
{
	OutReturnLocation = FVector::ZeroVector;
	if (!IsValid(HeldCrate) || !bHasHeldCrateReturnLocation || GetWorld() == nullptr)
	{
		return false;
	}

	const UStaticMeshComponent* CrateMesh = HeldCrate->GetCrateMeshComponent();
	const FVector MeshExtent = IsValid(CrateMesh)
		? CrateMesh->Bounds.BoxExtent
		: FVector(70.0f, 70.0f, 70.0f);
	// 바닥 자체는 충돌 후보에서 제외하고, 상자가 차지할 가로 면적만 검사합니다.
	const FCollisionShape FootprintShape = FCollisionShape::MakeBox(FVector(
		FMath::Max(70.0f, MeshExtent.X + 25.0f),
		FMath::Max(70.0f, MeshExtent.Y + 25.0f),
		25.0f));
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQuery.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StoreEmployeeCrateReturn), false);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(HeldCrate);

	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = GetActorRightVector().GetSafeNormal2D();
	const FVector SafeForward = Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
	const FVector SafeRight = Right.IsNearlyZero() ? FVector::RightVector : Right;
	const FVector OriginalLocation = HeldCrateReturnLocation;
	const TArray<FVector> CandidateOffsets = {
		-SafeForward * 220.0f,
		SafeRight * 180.0f,
		-SafeRight * 180.0f,
		SafeForward * 250.0f,
		-SafeForward * 320.0f,
		SafeRight * 300.0f,
		-SafeRight * 300.0f,
	};

	for (const FVector& Offset : CandidateOffsets)
	{
		const FVector CandidateLocation = OriginalLocation + Offset;
		// 바닥 높이보다 약간 위에서 검사해 바닥 메시 자체를 장애물로 오인하지 않습니다.
		const FVector TestLocation = CandidateLocation + FVector(0.0f, 0.0f, 35.0f);
		if (!GetWorld()->OverlapAnyTestByObjectType(
			TestLocation,
			FQuat::Identity,
			ObjectQuery,
			FootprintShape,
			QueryParams))
		{
			OutReturnLocation = CandidateLocation;
			return true;
		}
	}

	return false;
}

bool ACSStoreEmployeeCharacter::TryPackNearbyProducts(ACSProductPickupActor* SourceProduct)
{
	ACSShelfActor* Shelf = Cast<ACSShelfActor>(ActiveTask.TargetActor);
	ACSFreshnessSafeZoneActor* TargetStorage = Cast<ACSFreshnessSafeZoneActor>(ActiveTask.TargetActor);
	const bool bReorganizing = ActiveTask.Type == ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct;
	const bool bProtecting = ActiveTask.Type == ECSStoreEmployeeTaskType::ProtectPerishableStock;
	const bool bPackingTask = ActiveTask.Type == ECSStoreEmployeeTaskType::RestockShelf ||
		ActiveTask.Type == ECSStoreEmployeeTaskType::StoreLooseProduct || bReorganizing || bProtecting;
	if (!bPackingTask || (!IsValid(Shelf) && !IsValid(TargetStorage)) || !PackingCrateClass ||
		!IsValid(CrateCarryAnchor) || !ACSDeliveryCrateActor::CanPackProduct(SourceProduct, this, ActiveTask.TaskId)) return false;
	AActor* SourceParent = SourceProduct->GetAttachParentActor();
	ACSShelfActor* SourceShelf = Cast<ACSShelfActor>(SourceParent);
	if (IsValid(SourceParent) && !Cast<ACSFreshnessSafeZoneActor>(SourceParent) &&
		!(SourceShelf && (bReorganizing || bProtecting))) return false;
	const int32 Capacity = Shelf ? FMath::Min(ACSDeliveryCrateActor::PackingCapacity,
		Shelf->GetEmployeeRestockableSlotCount(ActiveTask.ProductRow, ActiveTask.bAllowCrossTierDuplicateFallback)) : ACSDeliveryCrateActor::PackingCapacity;
	if (Capacity < 2) return false;
	const float AllowedPickupDistance = SourceShelf && bReorganizing ? MisplacedShelfProductPickupMaxDistance : LooseProductPickupMaxDistance;
	TArray<ACSProductPickupActor*> Products = { SourceProduct };
	TArray<ACSProductPickupActor*> Candidates;
	for (TActorIterator<ACSProductPickupActor> It(GetWorld()); It; ++It)
	{
		ACSProductPickupActor* Product = *It;
		if (Product == SourceProduct || !ACSDeliveryCrateActor::CanPackProduct(Product) || Product->IsReservedByEmployee() ||
			Product->GetAttachParentActor() != SourceParent || Product->GetProductRow().DataTable != ActiveTask.ProductRow.DataTable ||
			Product->GetProductRow().RowName != ActiveTask.ProductRow.RowName ||
			(SourceShelf && bReorganizing && !SourceShelf->ShouldEmployeeReorganizeProduct(Product)) ||
			// 같은 진열대 재정리는 이미 선반·상품·오배치 상태를 모두 확인했으므로, 선반 끝 상품도 함께 회수합니다.
			(!bReorganizing && FVector::DistSquared(Product->GetActorLocation(), SourceProduct->GetActorLocation()) > FMath::Square(PackingSearchRadius)) ||
			FVector::DistSquared2D(GetActorLocation(), Product->GetActorLocation()) > FMath::Square(AllowedPickupDistance)) continue;
		// 바닥 상품은 벽 너머 재고를 회수하지 않게 시야를 확인하되, 같은 진열대 재정리는 상품끼리 가려도 함께 묶습니다.
		if (bReorganizing)
		{
			Candidates.Add(Product);
			continue;
		}
		FHitResult Hit;
		FCollisionQueryParams Query;
		Query.AddIgnoredActor(this);
		Query.AddIgnoredActor(SourceProduct);
		// 원래 진열대·창고의 선반 판이 상품 시야를 가려 포장을 막지 않도록 제외합니다.
		if (SourceParent) Query.AddIgnoredActor(SourceParent);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, GetPawnViewLocation(), Product->GetComponentsBoundingBox().GetCenter(), ECC_Visibility, Query);
		if (!bBlocked || Hit.GetActor() == Product) Candidates.Add(Product);
	}
	Candidates.Sort([SourceProduct](const ACSProductPickupActor& A, const ACSProductPickupActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), SourceProduct->GetActorLocation()) <
			FVector::DistSquared(B.GetActorLocation(), SourceProduct->GetActorLocation());
	});
	for (ACSProductPickupActor* Product : Candidates)
	{
		if (Products.Num() >= Capacity) break;
		Products.Add(Product);
	}
	if (Products.Num() < 2) return false;
	// 작은 상품을 모으더라도 상자는 기본 크기로 생성합니다.
	const FTransform Transform(FRotator::ZeroRotator, SourceProduct->GetActorLocation());
	ACSDeliveryCrateActor* Crate = GetWorld()->SpawnActorDeferred<ACSDeliveryCrateActor>(PackingCrateClass, Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Crate)) return false;
	if (!Crate->InitializePackedProducts(Products)) { Crate->Destroy(); return false; }
	Crate->FinishSpawning(Transform);
	if (IsValid(Crate) && TargetStorage && !TargetStorage->CanStoreCrate(Crate, true))
	{
		Crate->Destroy();
		return false;
	}
	UCSStoreEmployeeSubsystem* Employees = GetWorld()->GetSubsystem<UCSStoreEmployeeSubsystem>();
	if (!IsValid(Crate) || !Employees || !Employees->ReplaceAssignedTaskSource(ActiveTask.TaskId, this, Crate, Products.Num()))
	{
		if (IsValid(Crate)) Crate->Destroy();
		return false;
	}
	ActiveTask.SourceActor = Crate;
	ActiveTask.RequestedQuantity = RemainingRestockQuantity = Products.Num();
	// 낱개 집기에서 검증한 거리를 사용하며, 새 상자에 배송 상자의 거리 제한을 다시 적용하지 않습니다.
	HeldCrateReturnLocation = Transform.GetLocation();
	bHasHeldCrateReturnLocation = true;
	Crate->PrepareForPickupPhysics();
	Crate->SetHeldByPlayerForFreshness(true);
	Crate->SetActorEnableCollision(false);
	Crate->AttachToComponent(CrateCarryAnchor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	HeldCrate = Crate;
	// 예약 이전과 상자 초기화가 모두 성공한 뒤 원본 재고를 제거합니다.
	for (ACSProductPickupActor* Product : Products) ACSDeliveryCrateActor::RemovePackedSource(Product);
	UE_LOG(LogTemp, Log, TEXT("[StoreEmployee] %s 직원이 %d번 업무에서 상품 %d개를 상자로 묶었습니다."), *GetName(), ActiveTask.TaskId, Products.Num());
	return true;
}

bool ACSStoreEmployeeCharacter::PickUpSourceCrate()
{
	ACSDeliveryCrateActor* SourceCrate = Cast<ACSDeliveryCrateActor>(ActiveTask.SourceActor);
	if (IsValid(SourceCrate))
	{
		// 예약 이후 플레이어가 집거나 퀵바에 넣은 상자는 다시 가져가지 않습니다.
		if (Cast<ACSPlayerCharacter>(SourceCrate->GetAttachParentActor()) != nullptr)
		{
			return false;
		}
		if (SourceCrate->GetRemainingQuantity() <= 0 || !IsValid(CrateCarryAnchor))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreEmployee] %s 직원의 %d번 상자 집기 실패: 대상=%s, 남은 수량=%d, 운반 앵커 유효=%s"),
				*GetName(),
				ActiveTask.TaskId,
				*SourceCrate->GetName(),
				SourceCrate->GetRemainingQuantity(),
				IsValid(CrateCarryAnchor) ? TEXT("예") : TEXT("아니오"));
			return false;
		}

		// AI 이동과 같은 수평 거리로 집기 가능 여부를 확인합니다. 진열대·창고의 상품 액터는
		// 선반 높이만큼 Z가 다르므로 3D 거리를 사용하면 바로 앞 상자도 300cm 초과로 오판합니다.
		const float SourceDistance = FVector::Dist2D(GetActorLocation(), SourceCrate->GetActorLocation());
		float AllowedPickupDistance = SourcePickupMaxDistance;
		bool bUsingDeliveryPortalPickupException = false;

		FVector DeliveryApproachLocation = FVector::ZeroVector;
		if (TryGetDeliveryCrateApproachPoint(
			SourceCrate, DeliveryPortalPickupMaxDistance, DeliveryApproachLocation) &&
			FVector::DistSquared2D(GetActorLocation(), DeliveryApproachLocation) <=
				FMath::Square(DeliveryPortalApproachTolerance))
		{
			// 하역장에 남은 상자를 지정 접근 지점에서 집을 때만 범위를 넓힙니다.
			AllowedPickupDistance = DeliveryPortalPickupMaxDistance;
			bUsingDeliveryPortalPickupException = true;
		}

		if (SourceDistance > AllowedPickupDistance)
		{
			// 탐색용 NavMesh 위치와 실제 상자가 멀면 원격 회수를 막고, 다음 갱신에서 더 적절한 업무를 다시 찾습니다.
			UE_LOG(LogTemp, Warning,
			TEXT("[StoreEmployee] %s 직원의 %d번 상자 집기 실패: 대상=%s, 수평 거리=%.1fcm, 허용 거리=%.1fcm"),
				*GetName(),
				ActiveTask.TaskId,
				*SourceCrate->GetName(),
				SourceDistance,
				AllowedPickupDistance);
			return false;
		}

		if (ACSFreshnessSafeZoneActor* StoragePoint =
			Cast<ACSFreshnessSafeZoneActor>(SourceCrate->GetAttachParentActor()))
		{
			// 창고에 적재된 상자를 다시 보충에 사용할 때 점유 슬롯과 냉장·냉동 보호 등록을 함께 해제합니다.
			StoragePoint->RemoveStoredCrate(SourceCrate);
		}

		// 운반 중에는 물리·충돌이 직원 이동을 방해하지 않게 끄고, 신선도 시스템에는 손에 들린 상태임을 알려 줍니다.
		HeldCrateReturnLocation = SourceCrate->GetActorLocation();
		bHasHeldCrateReturnLocation = true;
		SourceCrate->PrepareForPickupPhysics();
		SourceCrate->SetHeldByPlayerForFreshness(true);
		SourceCrate->SetActorEnableCollision(false);
		SourceCrate->AttachToComponent(
			CrateCarryAnchor,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		HeldCrate = SourceCrate;
		UE_LOG(LogTemp, Log,
			TEXT("[StoreEmployee] %s 직원이 %d번 업무의 상자를 집었습니다: 대상=%s, 수평 거리=%.1fcm, 배송 접근 지점 예외=%s"),
			*GetName(),
			ActiveTask.TaskId,
			*SourceCrate->GetName(),
			SourceDistance,
			bUsingDeliveryPortalPickupException ? TEXT("적용") : TEXT("미적용"));
		return true;
	}

	ACSProductPickupActor* SourceProduct = Cast<ACSProductPickupActor>(ActiveTask.SourceActor);
	if (!IsValid(SourceProduct) || !IsValid(RightHandCarryAnchor))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreEmployee] %s 직원의 %d번 낱개 상품 집기 실패: 대상 유효=%s, 운반 앵커 유효=%s"),
			*GetName(),
			ActiveTask.TaskId,
			IsValid(SourceProduct) ? TEXT("예") : TEXT("아니오"),
			IsValid(RightHandCarryAnchor) ? TEXT("예") : TEXT("아니오"));
		return false;
	}

	// 상품이 진열 단 위에 있는 경우에도 직원 캡슐 원점과의 높이 차이로 실패하지 않게 수평 거리를 사용합니다.
	const float SourceDistance = FVector::Dist2D(GetActorLocation(), SourceProduct->GetActorLocation());
	const ACSShelfActor* SourceShelfForPickup = Cast<ACSShelfActor>(SourceProduct->GetAttachParentActor());
	const bool bIsMisplacedShelfProduct =
		ActiveTask.Type == ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct &&
		IsValid(SourceShelfForPickup) &&
		SourceShelfForPickup->ShouldEmployeeReorganizeProduct(SourceProduct);
	const float AllowedPickupDistance = bIsMisplacedShelfProduct
		? MisplacedShelfProductPickupMaxDistance
		: LooseProductPickupMaxDistance;
	if (SourceDistance > AllowedPickupDistance)
	{
		// 낱개 상품도 작업자가 실제로 닿을 수 있는 거리에서만 집도록 제한합니다.
		UE_LOG(LogTemp, Warning,
		TEXT("[StoreEmployee] %s 직원의 %d번 낱개 상품 집기 실패: 대상=%s, 수평 거리=%.1fcm, 허용 거리=%.1fcm"),
			*GetName(),
			ActiveTask.TaskId,
			*SourceProduct->GetName(),
			SourceDistance,
			AllowedPickupDistance);
		return false;
	}

	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<ACSPlayerCharacter> PlayerIt(World); PlayerIt; ++PlayerIt)
		{
			if (IsValid(*PlayerIt) && (*PlayerIt)->GetHeldActor() == SourceProduct)
			{
				// 대기 중이던 업무라도 플레이어가 먼저 집었다면 소유권을 존중하고 즉시 취소합니다.
				UE_LOG(LogTemp, Warning,
					TEXT("[StoreEmployee] %s 직원의 %d번 낱개 상품 집기 취소: 대상=%s을(를) 플레이어가 들고 있습니다."),
					*GetName(), ActiveTask.TaskId, *SourceProduct->GetName());
				return false;
			}
		}
	}

	if (TryPackNearbyProducts(SourceProduct)) return true;
	if (!IsValid(SourceProduct)) return false;

	if (ACSFreshnessSafeZoneActor* StoragePoint =
		Cast<ACSFreshnessSafeZoneActor>(SourceProduct->GetAttachParentActor()))
	{
		// 창고에 보관된 낱개 상품을 진열에 쓸 때에는 점유한 자동 격자 슬롯을 먼저 해제합니다.
		StoragePoint->RemoveStoredLooseProduct(SourceProduct);
	}
	else if (ACSShelfActor* SourceShelf = Cast<ACSShelfActor>(SourceProduct->GetAttachParentActor()))
	{
		const bool bIsNormalReorganization =
			ActiveTask.Type == ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct &&
			SourceShelf->ShouldEmployeeReorganizeProduct(SourceProduct);
		const bool bIsPerishableProtection =
			ActiveTask.Type == ECSStoreEmployeeTaskType::ProtectPerishableStock;
		if (!bIsNormalReorganization && !bIsPerishableProtection)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreEmployee] %s 직원의 %d번 낱개 상품 집기 취소: 대상=%s은(는) 일반 진열 업무가 회수할 수 없는 진열 상품입니다."),
				*GetName(), ActiveTask.TaskId, *SourceProduct->GetName());
			return false;
		}

		// 오배치 정리와 냉장·냉동 보호 업무만 진열대 점유를 해제해, 일반 보충 직원이 진열 상품을 가져가지 않게 합니다.
		SourceShelf->RemovePlacedItem_Implementation(SourceProduct);
	}
	else if (IsValid(SourceProduct->GetAttachParentActor()))
	{
		// 진열대·수레·플레이어 등 다른 시스템이 소유한 상품은 직원이 빼앗아 가지 않도록 막습니다.
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreEmployee] %s 직원의 %d번 낱개 상품 집기 취소: 대상=%s의 소유 액터=%s"),
			*GetName(),
			ActiveTask.TaskId,
			*SourceProduct->GetName(),
			*SourceProduct->GetAttachParentActor()->GetName());
		return false;
	}

	// 상자가 없을 때 선택한 바닥 상품은 새로 생성하지 않고, 해당 액터 자체를 오른손에 들어 재고 중복을 막습니다.
	HeldProductReturnTransform = SourceProduct->GetActorTransform();
	bHasHeldProductReturnTransform = true;
	SourceProduct->PrepareForPickupPhysics();
	SourceProduct->SetHeldByPlayerForFreshness(true);
	SourceProduct->SetActorEnableCollision(false);
	SourceProduct->AttachToComponent(
		RightHandCarryAnchor,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	HeldProduct = SourceProduct;
	bHeldProductWasSpawnedFromCrate = false;
	UE_LOG(LogTemp, Log,
		TEXT("[StoreEmployee] %s 직원이 %d번 업무의 낱개 상품을 집었습니다: 대상=%s, 수평 거리=%.1fcm"),
		*GetName(),
		ActiveTask.TaskId,
		*SourceProduct->GetName(),
		SourceDistance);
	return true;
}

bool ACSStoreEmployeeCharacter::TakeProductFromHeldCrate()
{
	if (!IsValid(HeldCrate) || HeldCrate->GetRemainingQuantity() <= 0 ||
		!IsValid(RightHandCarryAnchor))
	{
		return false;
	}

	ACSProductPickupActor* Product = HeldCrate->SpawnProductActor();
	if (!IsValid(Product))
	{
		return false;
	}

	// 상품은 오른손 소켓 앵커에 부착하여, 이후 손 애니메이션에 자연스럽게 따라가도록 합니다.
	Product->PrepareForPickupPhysics();
	Product->SetHeldByPlayerForFreshness(true);
	Product->SetActorEnableCollision(false);
	Product->AttachToComponent(
		RightHandCarryAnchor,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	HeldProduct = Product;
	bHeldProductWasSpawnedFromCrate = true;
	return true;
}

bool ACSStoreEmployeeCharacter::PlaceHeldProductOnShelf()
{
	ACSShelfActor* Shelf = Cast<ACSShelfActor>(ActiveTask.TargetActor);
	if (!IsValid(Shelf))
	{
		return false;
	}

	FVector SlotLocation = FVector::ZeroVector;
	if (!IsValid(HeldProduct))
	{
		if (!IsValid(HeldCrate) || HeldCrate->GetRemainingQuantity() <= 0 ||
			!Shelf->GetNextEmployeeRestockSlotLocation(
				ActiveTask.ProductRow,
				ActiveTask.bAllowCrossTierDuplicateFallback,
				SlotLocation))
		{
			return false;
		}

		// 상자를 계속 들고 있는 상태에서 상품 액터를 생성해 곧바로 진열 슬롯에 배치합니다.
		// 이 경로에서는 상품을 오른손 앵커에 부착하지 않습니다.
		ACSProductPickupActor* ProductFromCrate = HeldCrate->SpawnProductActor();
		if (!IsValid(ProductFromCrate) || !Shelf->TryPlaceEmployeeProduct(
			ProductFromCrate,
			ActiveTask.bAllowCrossTierDuplicateFallback))
		{
			if (IsValid(ProductFromCrate))
			{
				ProductFromCrate->Destroy();
			}
			return false;
		}

		if (!HeldCrate->ConsumeOne())
		{
			Shelf->RemovePlacedItem_Implementation(ProductFromCrate);
			ProductFromCrate->Destroy();
			return false;
		}

		UE_LOG(LogTemp, Verbose,
			TEXT("[StoreEmployee] %s 직원이 들고 있는 상자에서 상품을 바로 진열했습니다: 상자=%s"),
			*GetName(), *HeldCrate->GetName());
		return true;
	}

	ACSProductPickupActor* ProductToPlace = HeldProduct;
	if (!Shelf->GetNextEmployeeRestockSlotLocation(
		ActiveTask.ProductRow,
		ActiveTask.bAllowCrossTierDuplicateFallback,
		SlotLocation) ||
		!Shelf->TryPlaceEmployeeProduct(
			ProductToPlace,
			ActiveTask.bAllowCrossTierDuplicateFallback))
	{
		ClearHeldProduct();
		return false;
	}

	// 진열대 부착이 완료된 뒤에만 상자 수량을 차감해 상품 복제나 재고 유실을 방지합니다.
	ProductToPlace->SetHeldByPlayerForFreshness(false);
	HeldProduct = nullptr;
	if (IsValid(HeldCrate) && !HeldCrate->ConsumeOne())
	{
		// 수량 차감이 예외적으로 실패하면 방금 배치한 상품도 되돌려 무료 상품이 남지 않게 합니다.
		Shelf->RemovePlacedItem_Implementation(ProductToPlace);
		ProductToPlace->Destroy();
		return false;
	}

	return true;
}

void ACSStoreEmployeeCharacter::ReleaseHeldCrate()
{
	if (!IsValid(HeldCrate))
	{
		HeldCrate = nullptr;
		bHasHeldCrateReturnLocation = false;
		return;
	}

	// 남은 상자는 진열대 접근 지점이나 직원 발밑이 아니라, 집기 전의 안전한 원래 위치로 되돌립니다.
	HeldCrate->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (bHasHeldCrateReturnLocation)
	{
		HeldCrate->SetActorLocation(HeldCrateReturnLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	HeldCrate->SetHeldByPlayerForFreshness(false);
	HeldCrate->SetActorEnableCollision(true);
	HeldCrate->EnableWorldDropPhysics();
	HeldCrate = nullptr;
	bHasHeldCrateReturnLocation = false;
}

void ACSStoreEmployeeCharacter::ReleaseHeldProductToReturnLocation()
{
	if (!IsValid(HeldProduct))
	{
		HeldProduct = nullptr;
		bHasHeldProductReturnTransform = false;
		return;
	}

	HeldProduct->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (bHasHeldProductReturnTransform)
	{
		HeldProduct->SetActorTransform(
			HeldProductReturnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	HeldProduct->SetHeldByPlayerForFreshness(false);
	HeldProduct->SetActorEnableCollision(true);
	HeldProduct->EnableWorldDropPhysics();
	HeldProduct = nullptr;
	bHeldProductWasSpawnedFromCrate = false;
	bHasHeldProductReturnTransform = false;
}

void ACSStoreEmployeeCharacter::ClearHeldProduct()
{
	if (IsValid(HeldProduct))
	{
		if (bHeldProductWasSpawnedFromCrate)
		{
			// 상자에서 임시 생성한 상품은 배치 실패 시 제거하여 상자 수량과 맞춥니다.
			HeldProduct->Destroy();
		}
		else
		{
			// 바닥에서 집은 원본 상품은 실패 시 파괴하지 않고 원래처럼 월드 물리 상태로 되돌립니다.
			HeldProduct->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			HeldProduct->SetHeldByPlayerForFreshness(false);
			HeldProduct->SetActorEnableCollision(true);
			HeldProduct->EnableWorldDropPhysics();
		}
	}
	HeldProduct = nullptr;
	bHeldProductWasSpawnedFromCrate = false;
	bHasHeldProductReturnTransform = false;
}
