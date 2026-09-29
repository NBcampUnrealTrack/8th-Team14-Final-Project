#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Checkout/CSCheckoutCounterActor.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Ending/CSEndingActors.h"
#include "Features/Ending/CSEndingDisplayActor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "UObject/ConstructorHelpers.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "Features/Employees/CSEmployeeWaitingArea.h"
#include "Features/Delivery/CSDeliveryMagicCircleActor.h"
#include "Features/Cart/CSCartActor.h"
#include "Features/Inventory/CSQuickbarInventoryComponent.h"
#include "Features/Checkout/CSCheckoutCounterActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Storage/CSFreshnessSafeZoneActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"
#include "Features/Journal/CSJournalWorldActors.h"
#include "Features/Tools/CSUsableTool.h"
#include "Features/Tools/CSUsableToolActor.h"
#include "Features/Tools/CSMagicWandActor.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/Employees/CSStoreEmployeeSubsystem.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Data/CSProductTypes.h"
#include "Data/CSSaveTypes.h"
#include "Systems/SaveLoad/CSWorldStateSaveHandler.h"
#include "Features/Trash/CSTrashActor.h"
#include "Features/Trash/CSTrashCanActor.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "InputActionValue.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Systems/Audio/CSAudioSubsystem.h"

namespace
{
const FName BuildPlacementCollisionTag(TEXT("BuildPlacementCollision"));
const FName BuildPlacementAreaTag(TEXT("BuildPlacementArea"));
const FName BuildPlacementAccessZoneTag(TEXT("BuildPlacementAccessZone"));
const FName MagicWandAttachBoneName(TEXT("RightHand"));

bool TryCalculateProspectiveBox(
	const UBoxComponent* BoxComponent,
	const AActor* FacilityActor,
	const FVector& FacilityLocation,
	const FRotator& FacilityRotation,
	FTransform& OutBoxTransform,
	FVector& OutScaledExtent)
{
	if (!IsValid(BoxComponent) || !IsValid(FacilityActor) ||
		FacilityLocation.ContainsNaN() || FacilityRotation.ContainsNaN())
	{
		return false;
	}

	const FTransform RelativeTransform =
		BoxComponent->GetComponentTransform().GetRelativeTransform(
			FacilityActor->GetActorTransform());
	const FTransform ProspectiveFacilityTransform(
		FacilityRotation,
		FacilityLocation,
		FacilityActor->GetActorScale3D());
	OutBoxTransform = RelativeTransform * ProspectiveFacilityTransform;
	OutScaledExtent =
		BoxComponent->GetUnscaledBoxExtent() *
		OutBoxTransform.GetScale3D().GetAbs();

	return !OutBoxTransform.GetLocation().ContainsNaN() &&
		!OutBoxTransform.GetRotation().ContainsNaN() &&
		!OutScaledExtent.ContainsNaN() &&
		OutScaledExtent.GetMin() > UE_KINDA_SMALL_NUMBER;
}


//회전된 두 박스(OBB)의 XY 단면이 겹치는지 2D SAT 알고리즘으로 검사.
//서로의 모서리가 상대방 박스 외곽에 있어도 겹침을 정확히 판정해 내기 위해 사용
//종족마다 캡슐 높이가 달라 충돌 판정이 흔들리는 것을 막기 위해 XY 평면 면적만 정밀 검사하고, Z축은 층 구분용으로만 단순 비교.
bool DoesBoxOverlapBox2D(const FTransform& TransformA, const FVector& ScaledExtentA,const FTransform& TransformB, const FVector& ScaledExtentB)
{
	// 두 박스의 가로축(X)과 세로축(Y) 방향을 추출
	// 이 4개의 방향이 두 박스를 가르는 빈 공간이 있는지 테스트할 기준선이 됨
	FVector2D AxisA[2] = {
		FVector2D(TransformA.GetUnitAxis(EAxis::X)),
		FVector2D(TransformA.GetUnitAxis(EAxis::Y)) };
	FVector2D AxisB[2] = {
		FVector2D(TransformB.GetUnitAxis(EAxis::X)),
		FVector2D(TransformB.GetUnitAxis(EAxis::Y)) };

	// 추출한 축의 길이를 1로 맞추는 정규화를 수행함
	// 배치 회전은 보통 제자리 회전(Yaw)만 쓰므로 이미 길이가 1인 단위벡터이지만,
	// 비정상적으로 기울어진 시설이 들어와 축 길이가 0이 되는 상황을 막고,그럴 시 겹치지 않은 것으로 넘깁니다
	for (FVector2D& Axis : AxisA)
	{
		if (!Axis.Normalize())
		{
			return false;
		}
	}
	for (FVector2D& Axis : AxisB)
	{
		if (!Axis.Normalize())
		{
			return false;
		}
	}
	// CenterDelta: 두 박스의 중심점 사이가 얼만큼 떨어져 있는지 나타내는 거리 벡터
	const FVector2D CenterDelta(
		TransformB.GetLocation().X - TransformA.GetLocation().X,
		TransformB.GetLocation().Y - TransformA.GetLocation().Y);

	// HalfA, HalfB: 두 박스의 중심에서 끝부분까지의 길이(반지름 역할)
	const FVector2D HalfA(ScaledExtentA.X, ScaledExtentA.Y);
	const FVector2D HalfB(ScaledExtentB.X, ScaledExtentB.Y);

	// SAT(분리축 정리): 4개의 기준축에 두 박스를 각각 투영하여 길이를 검사
	const FVector2D TestAxes[4] = { AxisA[0], AxisA[1], AxisB[0], AxisB[1] };
	for (const FVector2D& TestAxis : TestAxes)
	{
		// ProjectedA, ProjectedB: 테스트할 특정 축에 A박스와 B박스의 절반 크기를 투영한 길이
		// 내적을 통해 각 박스의 로컬 축들이 기준 축에 얼마나 평행한지 비율을 구하고, 실제 길이를 곱해 산출
		const double ProjectedA =
			FMath::Abs(FVector2D::DotProduct(TestAxis, AxisA[0])) * HalfA.X +
			FMath::Abs(FVector2D::DotProduct(TestAxis, AxisA[1])) * HalfA.Y;
		const double ProjectedB =
			FMath::Abs(FVector2D::DotProduct(TestAxis, AxisB[0])) * HalfB.X +
			FMath::Abs(FVector2D::DotProduct(TestAxis, AxisB[1])) * HalfB.Y;
		if (FMath::Abs(FVector2D::DotProduct(TestAxis, CenterDelta)) >
			ProjectedA + ProjectedB)
		{
			return false;
		}
	}

	// Z축비교 로직
	// 위 4번의 SAT 루프를 통과했다면 바닥(XY)에서는 겹친 상태
	// 마지막으로 두 박스의 Z축 Max과 Min을 비교하여, 평면상으로는 겹쳐도 1층과 2층처럼 다른 층에 있어 물리적으로 닿지 않는 상황을 걸러냄
	const double MinA = TransformA.GetLocation().Z - ScaledExtentA.Z;
	const double MaxA = TransformA.GetLocation().Z + ScaledExtentA.Z;
	const double MinB = TransformB.GetLocation().Z - ScaledExtentB.Z;
	const double MaxB = TransformB.GetLocation().Z + ScaledExtentB.Z;

	// 두 박스의 Z축 범위가 서로 교차할 때만 true를 반환
	return MaxA >= MinB && MinA <= MaxB;
}

void PlayPlayerGameplaySFX(const AActor* WorldContextActor, ECSAudioEvent AudioEvent)
{
	if (!IsValid(WorldContextActor))
	{
		return;
	}

	if (UGameInstance* GameInstance = WorldContextActor->GetGameInstance();
		IsValid(GameInstance))
	{
		if (UCSAudioSubsystem* AudioSubsystem =
			GameInstance->GetSubsystem<UCSAudioSubsystem>(); IsValid(AudioSubsystem))
		{
			AudioSubsystem->PlaySFX(AudioEvent);
		}
	}
}
}

ACSPlayerCharacter::ACSPlayerCharacter()
{
	static ConstructorHelpers::FClassFinder<ACSDeliveryCrateActor> PackingClass(TEXT("/Game/ConvenienceStore/Blueprints/Delivery/BP_DeliveryCrateBase"));
	PackingCrateClass = PackingClass.Class;
    // 진열 프리뷰는 물체를 들고 있을 때만 갱신합니다.
    PrimaryActorTick.bCanEverTick = true;
	// 상호작용 안내는 물체를 들지 않은 상태에서도 갱신해야 하므로 항상 Tick을 유지합니다.
    PrimaryActorTick.bStartWithTickEnabled = true;

    // 캐릭터 몸체는 회전하지 않고, 카메라 시점만 회전시킵니다.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = true;
    bUseControllerRotationRoll = false;

    // 이동 방향으로 자동 회전하지 않게 합니다.
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed = 700.0f;

    // 기본 Character 웅크리기 기능이 충돌과 천장 공간을 안전하게 처리하도록 활성화합니다.
    GetCharacterMovement()->NavAgentProps.bCanCrouch = true;

    // 플레이어·손님·직원은 서로 통과하고, 벽·진열대 등 월드 충돌은 유지합니다.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(
        TEXT("FirstPersonCamera"));

    // 카메라를 캡슐 컴포넌트에 연결합니다.
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(-10.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    HeldItemAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("HeldItemAnchor"));
    HeldItemAnchor->SetupAttachment(FirstPersonCamera);
    HeldItemAnchor->SetRelativeLocation(FVector(80.0f, 20.0f, -20.0f));

	MagicWandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("MagicWandAnchor"));
	MagicWandAnchor->SetupAttachment(GetMesh(), MagicWandAttachBoneName);
	MagicWandAnchor->SetRelativeTransform(FTransform::Identity);
	// 손 위치/회전만 따라가고, 비균일 Player Mesh Scale은 마법봉에 전달하지 않습니다.
	MagicWandAnchor->SetAbsolute(false, false, true);

	MagicWandHandTarget = CreateDefaultSubobject<USceneComponent>(TEXT("MagicWandHandTarget"));
	MagicWandHandTarget->SetupAttachment(FirstPersonCamera);
	MagicWandElbowTarget = CreateDefaultSubobject<USceneComponent>(TEXT("MagicWandElbowTarget"));
	MagicWandElbowTarget->SetupAttachment(GetCapsuleComponent());

	CartPushAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("CartPushAnchor"));
	CartPushAnchor->SetupAttachment(GetCapsuleComponent());
	// 기본값은 편집기에서 보이는 기준점이며, 실제 손잡이 높이는 플레이어 BP에서 조절합니다.
	CartPushAnchor->SetRelativeLocation(FVector(120.0f, 0.0f, -72.0f));

	QuickbarStorageAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("QuickbarStorageAnchor"));
	QuickbarStorageAnchor->SetupAttachment(GetRootComponent());

	QuickbarInventory = CreateDefaultSubobject<UCSQuickbarInventoryComponent>(TEXT("QuickbarInventory"));

	BuildPlacementPreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("BuildPlacementPreviewMesh"));
	BuildPlacementPreviewMesh->SetupAttachment(GetRootComponent());
	BuildPlacementPreviewMesh->SetAbsolute(true, true, true);
	BuildPlacementPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BuildPlacementPreviewMesh->SetGenerateOverlapEvents(false);
	BuildPlacementPreviewMesh->SetCanEverAffectNavigation(false);
	BuildPlacementPreviewMesh->SetCastShadow(false);
	BuildPlacementPreviewMesh->SetHiddenInGame(true);
	BuildPlacementPreviewMesh->SetVisibility(false);

    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    // 웅크린 상태에서는 일반 걷기보다 느린 이동 속도를 적용합니다.
    GetCharacterMovement()->MaxWalkSpeedCrouched = CrouchSpeed;
    // 점프 높이를 결정합니다.
    GetCharacterMovement()->JumpZVelocity = 700.0f;
    GetCharacterMovement()->AirControl = 0.35f;
}

void ACSPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
	RefreshArtifactMoveSpeed();

	if (IsValid(QuickbarInventory))
	{
		QuickbarInventory->OnQuickbarChanged.AddUniqueDynamic(
			this, &ACSPlayerCharacter::HandleQuickbarChanged);
		QuickbarInventory->OnQuickbarSelectionChanged.AddUniqueDynamic(
			this, &ACSPlayerCharacter::HandleQuickbarSelectionChanged);
		QuickbarInventory->InitializeQuickbar();
	}
	if (ACSStoreGameState* StoreState = GetWorld()->GetGameState<ACSStoreGameState>())
	{
		StoreState->OnStoreLevelChanged.AddUniqueDynamic(this, &ACSPlayerCharacter::HandleStoreLevelChanged);
		HandleStoreLevelChanged(StoreState->GetStoreLevel());
	}

    // 로컬 플레이어에게만 Input Mapping Context를 적용합니다.
    if (APlayerController* PlayerController =
        Cast<APlayerController>(GetController()))
    {
        if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
                LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                InputSubsystem->AddMappingContext(PlayerMappingContext, 0);
            }
        }
    }

	// 정적 레벨 구성인 건축 가능 영역을 한 번 캐시하고, 건축모드 진입 때 갱신합니다.
	RefreshBuildPlacementAreaCache();
}

void ACSPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACSStoreGameState* StoreState = GetWorld()->GetGameState<ACSStoreGameState>())
	{
		StoreState->OnStoreLevelChanged.RemoveDynamic(this, &ACSPlayerCharacter::HandleStoreLevelChanged);
	}
	if (AimedEndingDisplay.IsValid()) AimedEndingDisplay->HidePlacementPreview();
	if (IsValid(PushedCart))
	{
		// 플레이어가 제거될 때 수레의 Pawn 충돌 응답도 원래대로 되돌립니다.
		PushedCart->StopPushing(this);
		PushedCart = nullptr;
	}
	ResetDropActionHoldState();
	ResetBuildRotationInput();
	RestoreHeldBuildableActorStates();
	ClearBuildPlacementPreview();
	CachedBuildPlacementAreas.Reset();
	bBuildPlacementAreaCacheInitialized = false;
	// 플레이어가 제거될 때 프리뷰 액터도 함께 정리하여 월드에 남지 않게 합니다.
	DestroyPlacementPreview();

	Super::EndPlay(EndPlayReason);
}

void ACSPlayerCharacter::SetupPlayerInputComponent(
    UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* EnhancedInputComponent =
        CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

    EnhancedInputComponent->BindAction(
        MoveAction, ETriggerEvent::Triggered, this, &ACSPlayerCharacter::Move);

    EnhancedInputComponent->BindAction(
        LookAction, ETriggerEvent::Triggered, this, &ACSPlayerCharacter::Look);

    EnhancedInputComponent->BindAction(
    JumpAction, ETriggerEvent::Triggered, this, &ACSPlayerCharacter::Jump);

    EnhancedInputComponent->BindAction(
    JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

    EnhancedInputComponent->BindAction(
        SprintAction, ETriggerEvent::Started, this, &ACSPlayerCharacter::StartSprint);

    EnhancedInputComponent->BindAction(
        SprintAction, ETriggerEvent::Completed, this, &ACSPlayerCharacter::StopSprint);

    // Ctrl을 누르는 동안만 웅크리고, 입력을 놓으면 일어서기를 시도합니다.
    EnhancedInputComponent->BindAction(
        CrouchAction, ETriggerEvent::Started, this, &ACSPlayerCharacter::StartCrouch);

    EnhancedInputComponent->BindAction(
        CrouchAction, ETriggerEvent::Completed, this, &ACSPlayerCharacter::StopCrouch);

    EnhancedInputComponent->BindAction(
        InteractAction, ETriggerEvent::Started, this, &ACSPlayerCharacter::TryInteract);

	EnhancedInputComponent->BindAction(
		DropAction, ETriggerEvent::Started, this, &ACSPlayerCharacter::HandleDropActionStarted);
	EnhancedInputComponent->BindAction(
		DropAction, ETriggerEvent::Completed, this, &ACSPlayerCharacter::HandleDropActionReleased);
	EnhancedInputComponent->BindAction(
		DropAction, ETriggerEvent::Canceled, this, &ACSPlayerCharacter::HandleDropActionCanceled);

	if (ToggleBuildModeAction != nullptr)
	{
		EnhancedInputComponent->BindAction(
			ToggleBuildModeAction,
			ETriggerEvent::Started,
			this,
			&ACSPlayerCharacter::ToggleBuildMode);
	}

	if (RotateBuildLeftAction != nullptr)
	{
		EnhancedInputComponent->BindAction(
			RotateBuildLeftAction,
			ETriggerEvent::Started,
			this,
			&ACSPlayerCharacter::StartRotateBuildLeft);
		EnhancedInputComponent->BindAction(
			RotateBuildLeftAction,
			ETriggerEvent::Completed,
			this,
			&ACSPlayerCharacter::StopRotateBuildLeft);
		EnhancedInputComponent->BindAction(
			RotateBuildLeftAction,
			ETriggerEvent::Canceled,
			this,
			&ACSPlayerCharacter::StopRotateBuildLeft);
	}

	if (RotateBuildRightAction != nullptr)
	{
		EnhancedInputComponent->BindAction(
			RotateBuildRightAction,
			ETriggerEvent::Started,
			this,
			&ACSPlayerCharacter::StartRotateBuildRight);
		EnhancedInputComponent->BindAction(
			RotateBuildRightAction,
			ETriggerEvent::Completed,
			this,
			&ACSPlayerCharacter::StopRotateBuildRight);
		EnhancedInputComponent->BindAction(
			RotateBuildRightAction,
			ETriggerEvent::Canceled,
			this,
			&ACSPlayerCharacter::StopRotateBuildRight);
	}

	if (PrimaryUseAction != nullptr)
	{
		EnhancedInputComponent->BindAction(
			PrimaryUseAction, ETriggerEvent::Started, this, &ACSPlayerCharacter::TryUsePrimaryTool);
	}

	if (ToggleCartPushAction != nullptr)
	{
		EnhancedInputComponent->BindAction(
			ToggleCartPushAction,
			ETriggerEvent::Started,
			this,
			&ACSPlayerCharacter::ToggleCartPush);
	}

	const auto BindQuickbarAction = [this, EnhancedInputComponent](
		const UInputAction* InputAction,
		void (ACSPlayerCharacter::* Handler)())
	{
		if (InputAction != nullptr)
		{
			EnhancedInputComponent->BindAction(InputAction, ETriggerEvent::Started, this, Handler);
		}
	};
	BindQuickbarAction(SelectQuickbarSlot1Action, &ACSPlayerCharacter::SelectQuickbarSlot1);
	BindQuickbarAction(SelectQuickbarSlot2Action, &ACSPlayerCharacter::SelectQuickbarSlot2);
	BindQuickbarAction(SelectQuickbarSlot3Action, &ACSPlayerCharacter::SelectQuickbarSlot3);
	BindQuickbarAction(SelectQuickbarSlot4Action, &ACSPlayerCharacter::SelectQuickbarSlot4);
	BindQuickbarAction(SelectQuickbarSlot5Action, &ACSPlayerCharacter::SelectQuickbarSlot5);
	BindQuickbarAction(SelectQuickbarSlot6Action, &ACSPlayerCharacter::SelectQuickbarSlot6);
	BindQuickbarAction(CycleQuickbarNextAction, &ACSPlayerCharacter::SelectNextQuickbarSlot);
	BindQuickbarAction(CycleQuickbarPreviousAction, &ACSPlayerCharacter::SelectPreviousQuickbarSlot);

}

void ACSPlayerCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D MovementVector = Value.Get<FVector2D>();

    if (Controller == nullptr)
    {
        return;
    }

    // 카메라가 보는 수평 방향을 기준으로 이동합니다.
    const FRotator ControlRotation = Controller->GetControlRotation();
    const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);

    const FVector ForwardDirection =
        FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

    const FVector RightDirection =
        FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

    AddMovementInput(ForwardDirection, MovementVector.X);
	AddMovementInput(RightDirection, MovementVector.Y);
}

void ACSPlayerCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D LookAxisVector = Value.Get<FVector2D>();

    AddControllerYawInput(LookAxisVector.X * MouseSensitivity);
    AddControllerPitchInput(LookAxisVector.Y * MouseSensitivity);
}

void ACSPlayerCharacter::StartSprint()
{
	// 웅크린 상태에서는 달리기를 시작하지 않도록 제한합니다.
	if (bIsCrouched)
	{
		return;
	}

	bArtifactSprinting = true;
	RefreshArtifactMoveSpeed();
}

void ACSPlayerCharacter::StopSprint()
{
	bArtifactSprinting = false;
	RefreshArtifactMoveSpeed();
}

void ACSPlayerCharacter::RefreshArtifactMoveSpeed()
{
	const UCSArtifactSubsystem* Artifacts = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>() : nullptr;
	const float Multiplier = Artifacts ? Artifacts->GetSpeedMultiplier(ECSArtifactEffectType::PlayerSpeed) : 1.0f;
	GetCharacterMovement()->MaxWalkSpeed = (bArtifactSprinting ? SprintSpeed : WalkSpeed) * Multiplier;
	GetCharacterMovement()->MaxWalkSpeedCrouched = CrouchSpeed * Multiplier;
}

void ACSPlayerCharacter::StartCrouch()
{
	// 웅크리기를 시작할 때는 현재 달리기를 즉시 해제합니다.
	StopSprint();

	// 기본 Crouch 함수가 Capsule 충돌과 카메라 기준 높이를 함께 처리합니다.
	Crouch();
}

void ACSPlayerCharacter::StopCrouch()
{
	// 낮은 천장에 막힌 경우에는 Unreal 기본 UnCrouch 함수가 웅크린 상태를 유지합니다. (기본기능)
	UnCrouch();
}


void ACSPlayerCharacter::ToggleBuildMode()
{
	if (IsValid(HeldActor))
	{
		return;
	}

	CurrentPlayerMode = IsBuildMode()
		? ECSPlayerMode::Normal
		: ECSPlayerMode::Build;
	if (IsBuildMode())
	{
		RefreshBuildPlacementAreaCache();
	}
	//B 입력 직후 모드 아이콘과 전환 애니메이션을 동기화합니다.
	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController()))
	{
		StorePlayerController->SetBuildModeHUDState(IsBuildMode(), true);
	}
	ResetBuildRotationInput();
	ClearBuildPlacementPreview();
	DestroyPlacementPreview();

	// 건축 시스템의 성공 상태만 방송하고, 튜토리얼 진행 판단은 구독 측에 맡깁니다.
	OnBuildModeChanged.Broadcast(IsBuildMode());
}

void ACSPlayerCharacter::StartRotateBuildLeft()
{
	if (!IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>())
	{
		return;
	}

	bBuildRotateLeftHeld = true;
	BuildRotateLeftHeldSeconds = 0.0f;
	BuildRotationAccumulator = 0.0f;
	ApplyBuildRotationStep(-1.0f);
}

void ACSPlayerCharacter::StopRotateBuildLeft()
{
	bBuildRotateLeftHeld = false;
	BuildRotateLeftHeldSeconds = 0.0f;
	BuildRotationAccumulator = 0.0f;
}

void ACSPlayerCharacter::StartRotateBuildRight()
{
	if (!IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>())
	{
		return;
	}

	bBuildRotateRightHeld = true;
	BuildRotateRightHeldSeconds = 0.0f;
	BuildRotationAccumulator = 0.0f;
	ApplyBuildRotationStep(1.0f);
}

void ACSPlayerCharacter::StopRotateBuildRight()
{
	bBuildRotateRightHeld = false;
	BuildRotateRightHeldSeconds = 0.0f;
	BuildRotationAccumulator = 0.0f;
}

void ACSPlayerCharacter::ApplyBuildRotationStep(float YawDelta)
{
	if (!IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>() ||
		!FMath::IsFinite(YawDelta))
	{
		return;
	}

	HeldBuildableFacilityRotation.Yaw = FRotator::NormalizeAxis(
		HeldBuildableFacilityRotation.Yaw + YawDelta);
}

void ACSPlayerCharacter::UpdateBuildRotationInput(float DeltaTime)
{
	if (!IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>())
	{
		ResetBuildRotationInput();
		return;
	}

	const float SafeDeltaTime = FMath::Max(0.0f, DeltaTime);
	const float PreviousLeftHeldSeconds = BuildRotateLeftHeldSeconds;
	const float PreviousRightHeldSeconds = BuildRotateRightHeldSeconds;
	if (bBuildRotateLeftHeld)
	{
		BuildRotateLeftHeldSeconds += SafeDeltaTime;
	}
	if (bBuildRotateRightHeld)
	{
		BuildRotateRightHeldSeconds += SafeDeltaTime;
	}

	if (bBuildRotateLeftHeld == bBuildRotateRightHeld)
	{
		BuildRotationAccumulator = 0.0f;
		return;
	}

	const bool bRotateLeft = bBuildRotateLeftHeld;
	const float PreviousHeldSeconds = bRotateLeft
		? PreviousLeftHeldSeconds
		: PreviousRightHeldSeconds;
	const float CurrentHeldSeconds = bRotateLeft
		? BuildRotateLeftHeldSeconds
		: BuildRotateRightHeldSeconds;
	const float HoldDelay = FMath::Max(0.0f, BuildRotationHoldDelay);
	const float RotationDeltaTime = FMath::Max(
		0.0f,
		CurrentHeldSeconds - FMath::Max(PreviousHeldSeconds, HoldDelay));
	if (RotationDeltaTime <= 0.0f)
	{
		return;
	}

	BuildRotationAccumulator +=
		FMath::Max(0.0f, BuildRotationDegreesPerSecond) * RotationDeltaTime;
	const float StepDirection = bRotateLeft ? -1.0f : 1.0f;
	while (BuildRotationAccumulator >= 1.0f)
	{
		ApplyBuildRotationStep(StepDirection);
		BuildRotationAccumulator -= 1.0f;
	}
}

void ACSPlayerCharacter::ResetBuildRotationInput()
{
	bBuildRotateLeftHeld = false;
	bBuildRotateRightHeld = false;
	BuildRotateLeftHeldSeconds = 0.0f;
	BuildRotateRightHeldSeconds = 0.0f;
	BuildRotationAccumulator = 0.0f;
}

void ACSPlayerCharacter::UpdateHeldMagicWandDockHUD()
{
	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	if (!IsValid(StorePlayerController))
	{
		return;
	}

	StorePlayerController->ClearInteractionContext();
	if (StorePlayerController->IsGameplayUIInputLocked())
	{
		return;
	}

	const ACSMagicWandActor* MagicWand = Cast<ACSMagicWandActor>(HeldActor);
	if (IsValid(MagicWand) && IsAimingAtMagicWandHomeDock(MagicWand))
	{
		StorePlayerController->SetInteractionPrompt(
			NSLOCTEXT(
				"Interaction",
				"DockMagicWandPrompt",
				"[Q] 마법봉 내려놓기"),
			FLinearColor::White);
	}
}

bool ACSPlayerCharacter::IsAimingAtMagicWandHomeDock(
	const ACSMagicWandActor* MagicWand) const
{
	if (!IsValid(MagicWand) || FirstPersonCamera == nullptr || GetWorld() == nullptr)
	{
		return false;
	}

	const USceneComponent* HomeComponent = MagicWand->GetHomeDockComponent();
	const AActor* HomeFacility = MagicWand->GetHomeDockOwner();
	if (!IsValid(HomeComponent) ||
		!IsValid(HomeFacility) ||
		HomeComponent->GetOwner() != HomeFacility)
	{
		return false;
	}

	const FVector TraceStart = FirstPersonCamera->GetComponentLocation();
	const FVector TraceEnd =
		TraceStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;
	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(MagicWand);
	if (!GetWorld()->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams))
	{
		return false;
	}

	static const FName MagicWandDockTag(TEXT("MagicWandDock"));
	const UPrimitiveComponent* HitComponent = HitResult.GetComponent();
	return IsValid(HitComponent) &&
		HitComponent->ComponentHasTag(MagicWandDockTag) &&
		HitComponent->GetOwner() == HomeFacility;
}

bool ACSPlayerCharacter::RestoreMagicWandToHomeDock(ACSMagicWandActor* MagicWand)
{
	if (!IsValid(MagicWand))
	{
		return false;
	}

	USceneComponent* HomeComponent = MagicWand->GetHomeDockComponent();
	AActor* HomeFacility = MagicWand->GetHomeDockOwner();
	if (!IsValid(HomeComponent) ||
		!IsValid(HomeFacility) ||
		HomeComponent->GetOwner() != HomeFacility)
	{
		return false;
	}

	const USceneComponent* WandRoot = MagicWand->GetRootComponent();
	if (IsValid(WandRoot) && WandRoot->GetAttachParent() == HomeComponent)
	{
		return true;
	}

	const bool bWasHeldByPlayer = HeldActor == MagicWand;
	MagicWand->PrepareForPickupPhysics();
	if (!MagicWand->AttachToComponent(
		HomeComponent,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale))
	{
		if (!bWasHeldByPlayer)
		{
			MagicWand->EnableWorldDropPhysics();
		}
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 원래 Home Dock Component에 마법봉을 다시 부착하지 못했습니다."),
			*MagicWand->GetName());
		return false;
	}

	MagicWand->OnUnequipped();
	MagicWand->SetActorEnableCollision(true);
	return true;
}

void ACSPlayerCharacter::RestoreFacilityMagicWands(AActor* FacilityActor)
{
	if (!IsValid(FacilityActor))
	{
		return;
	}

	// 런타임 Spawn 후 일반 SceneComponent Anchor에 붙은 마법봉도 시설 이동 전에 복귀 상태를 확정합니다.
	TArray<AActor*> RuntimeAttachedActors;
	FacilityActor->GetAttachedActors(RuntimeAttachedActors, true, true);
	for (AActor* AttachedActor : RuntimeAttachedActors)
	{
		if (ACSMagicWandActor* MagicWand = Cast<ACSMagicWandActor>(AttachedActor))
		{
			MagicWand->RememberHomeDockFromCurrentAttachment();
			RestoreMagicWandToHomeDock(MagicWand);
		}
	}

	TSet<AActor*> VisitedActors;
	TFunction<void(AActor*)> RestoreChildMagicWands;
	RestoreChildMagicWands =
		[this, &VisitedActors, &RestoreChildMagicWands](AActor* SourceActor)
	{
		if (!IsValid(SourceActor) || VisitedActors.Contains(SourceActor))
		{
			return;
		}

		VisitedActors.Add(SourceActor);
		TArray<UChildActorComponent*> ChildActorComponents;
		SourceActor->GetComponents<UChildActorComponent>(ChildActorComponents);
		for (UChildActorComponent* ChildActorComponent : ChildActorComponents)
		{
			if (!IsValid(ChildActorComponent))
			{
				continue;
			}

			AActor* ChildActor = ChildActorComponent->GetChildActor();
			if (!IsValid(ChildActor))
			{
				continue;
			}

			if (ACSMagicWandActor* MagicWand = Cast<ACSMagicWandActor>(ChildActor))
			{
				// 기존 ChildActorComponent 구성도 동일한 Home Dock 저장 경로로 계속 지원합니다.
				MagicWand->RememberHomeDockFromCurrentAttachment();
				RestoreMagicWandToHomeDock(MagicWand);
			}
			else
			{
				RestoreChildMagicWands(ChildActor);
			}
		}
	};

	RestoreChildMagicWands(FacilityActor);
}

void ACSPlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
	if (AimedEndingDisplay.IsValid()) AimedEndingDisplay->HidePlacementPreview();
	AimedEndingDisplay.Reset();
	// 아래 모드별 early return과 무관하게 플레이어 이동 애니메이션은 항상 먼저 갱신합니다.
	UpdatePlayerLocomotionAnimation();

	if (IsBuildMode())
	{
		UpdateBuildRotationInput(DeltaTime);
		UpdateBuildPlacementPreview();
		// 판정이 끝난 뒤에 그려야 침범 중인 영역을 같은 프레임에 침범 영역 색으로 변경
		RefreshInstalledAccessZoneDisplays();
		UpdateBuildModeHUD();
		return;
	}

	if (IsValid(PushedCart))
	{
		// 수레는 손에 붙이지 않고 손잡이 기준점을 향해 Sweep 이동해 바닥과 벽 충돌을 유지합니다.
		PushedCart->UpdatePushTransform(CartPushAnchor);
		if (ACSStorePlayerController* StorePlayerController =
			Cast<ACSStorePlayerController>(GetController()))
		{
			StorePlayerController->ClearInteractionContext();
			StorePlayerController->SetInteractionPrompt(
				NSLOCTEXT("Interaction", "ReleaseCartPrompt", "[F] 수레 놓기"),
				FLinearColor::White);
		}
		return;
	}

	if (IsValid(HeldActor))
	{
		if (HeldActor->IsA<ACSEndingItemActor>())
		{
			// 엔딩 재료를 운반할 때도 마법사와 구매처의 E 안내를 갱신합니다.
			UpdateInteractionHUD();
		}
		else if (HeldActor->Implements<UCSUsableTool>())
		{
			// 마법봉은 원래 거치대를 조준한 경우에만 Q 안내를 표시합니다.
			UpdateHeldMagicWandDockHUD();
		}
		else
		{
			UpdatePlacementPreview();
			// 프리뷰가 안내를 초기화한 뒤 E 회수와 Q 진열 안내를 함께 작성합니다.
			UpdateHeldCollectionHUD();
		}
	}
	else
	{
		UpdateInteractionHUD();
	}
}

void ACSPlayerCharacter::UpdatePlayerLocomotionAnimation(bool bForce)
{
	constexpr float WalkEnterSpeed = 10.0f;
	constexpr float IdleEnterSpeed = 5.0f;
	const float HorizontalSpeed = GetVelocity().Size2D();
	if (bPlayerWalkingAnimation)
	{
		if (HorizontalSpeed <= IdleEnterSpeed)
		{
			bPlayerWalkingAnimation = false;
		}
	}
	else if (HorizontalSpeed >= WalkEnterSpeed)
	{
		bPlayerWalkingAnimation = true;
	}

	UAnimSequence* DesiredAnimation =
		bPlayerWalkingAnimation ? PlayerWalkAnimation.Get() : PlayerIdleAnimation.Get();
	if (!IsValid(DesiredAnimation))
	{
		DesiredAnimation =
			bPlayerWalkingAnimation ? PlayerIdleAnimation.Get() : PlayerWalkAnimation.Get();
	}

	if (!IsValid(DesiredAnimation) ||
		(!bForce && CurrentPlayerLocomotionAnimation == DesiredAnimation))
	{
		return;
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->PlayAnimation(DesiredAnimation, true);
		CurrentPlayerLocomotionAnimation = DesiredAnimation;
	}
}

bool ACSPlayerCharacter::TryPickUp(AActor* PickupActor)
{
	USceneComponent* AttachTarget = GetHeldItemAttachTarget(PickupActor);
	if (!IsValid(AttachTarget))
	{
		return false;
	}
	const bool bQuickbarCargo = !IsBuildMode() &&
		(Cast<ACSDeliveryCrateActor>(PickupActor) != nullptr || Cast<ACSTrashActor>(PickupActor) != nullptr);
	if (bQuickbarCargo && IsValid(HeldActor) && Cast<ACSUsableToolActor>(HeldActor) != nullptr)
	{
		StoreEquippedQuickbarTool();
	}

	if (ACSUsableToolActor* ToolActor = Cast<ACSUsableToolActor>(PickupActor);
		!IsBuildMode() &&
		IsValid(ToolActor) &&
		IsValid(QuickbarInventory) &&
		IsValid(QuickbarStorageAnchor))
	{
		// 상품·상자처럼 물리적으로 든 대상이 있으면 도구를 퀵바에 넣지 않습니다.
		if (IsValid(HeldActor) && Cast<ACSUsableToolActor>(HeldActor) == nullptr)
		{
			return false;
		}

		const int32 AddedSlotIndex = QuickbarInventory->TryAddTool(ToolActor);
		if (AddedSlotIndex == INDEX_NONE)
		{
			return false;
		}

		if (ACSMagicWandActor* MagicWand = Cast<ACSMagicWandActor>(ToolActor))
		{
			// Quickbar Anchor로 옮기기 전에 현재 WandAnchor를 원래 Home Dock으로 보존합니다.
			MagicWand->RememberHomeDockFromCurrentAttachment();
		}

		// 기존에 손에 든 도구가 있다면 먼저 같은 퀵바에 안전하게 보관합니다.
		StoreEquippedQuickbarTool();
		ToolActor->StoreInQuickbar(QuickbarStorageAnchor);
		// 새로 주운 도구를 즉시 사용할 수 있게 해당 슬롯을 선택합니다.
		QuickbarInventory->SelectSlot(AddedSlotIndex);
		EquipSelectedQuickbarTool();
		return true;
	}

	if (IsBuildMode() ||
		!IsValid(PickupActor) ||
		PickupActor->Implements<UCSBuildableFacility>() ||
		IsValid(PushedCart) ||
		IsValid(HeldActor) ||
		HeldItemAnchor == nullptr)
    {
        return false;
    }

	PrepareInventoryForPlayerPickup(PickupActor);

    // 진열대에서 다시 집은 상품은 기존 슬롯 점유를 먼저 해제합니다.
	if (AActor* PreviousParent = PickupActor->GetAttachParentActor();
		IsValid(PreviousParent) && PreviousParent->Implements<UCSShelfPlaceable>())
	{
		ICSShelfPlaceable::Execute_RemovePlacedItem(PreviousParent, PickupActor);
	}
	else if (ACSFreshnessSafeZoneActor* StoragePoint =
		Cast<ACSFreshnessSafeZoneActor>(PickupActor->GetAttachParentActor()))
	{
		// 창고 격자에 있던 낱개 상품을 집으면 점유 배열과 냉장·냉동 보호 등록을 함께 해제합니다.
		if (ACSProductPickupActor* StoredProduct = Cast<ACSProductPickupActor>(PickupActor))
		{
			StoragePoint->RemoveStoredLooseProduct(StoredProduct);
		}
		else if (ACSDeliveryCrateActor* StoredCrate = Cast<ACSDeliveryCrateActor>(PickupActor))
		{
			StoragePoint->RemoveStoredCrate(StoredCrate);
		}
	}

	// 물리로 분리된 메시를 먼저 복원한 뒤 Actor 자체를 손 위치에 부착합니다.
	if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(PickupActor))
	{
		ProductActor->PrepareForPickupPhysics();
		ProductActor->SetHeldByPlayerForFreshness(true);
	}
	else if (ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(PickupActor))
	{
		DeliveryCrate->PrepareForPickupPhysics();
		DeliveryCrate->SetHeldByPlayerForFreshness(true);
	}
	else if (ACSUsableToolActor* ToolActor = Cast<ACSUsableToolActor>(PickupActor))
	{
		if (ACSMagicWandActor* MagicWand = Cast<ACSMagicWandActor>(ToolActor))
		{
			// 최초 집기 직전의 WandAnchor/ChildActorComponent를 기억하며 기존 유효값은 유지합니다.
			MagicWand->RememberHomeDockFromCurrentAttachment();
		}
		// 물리 상태의 루트 메시를 부착하면 실패할 수 있으므로 부착 전에 먼저 물리를 해제합니다.
		ToolActor->PrepareForPickupPhysics();
	}
	else if (ACSTrashActor* TrashActor = Cast<ACSTrashActor>(PickupActor))
	{
		// 튜토리얼 청소 단계에서 줍는 쓰레기 (상품과 동일한 방식으로 물리 정리)
		TrashActor->PrepareForPickupPhysics();
	}

	PickupActor->SetActorEnableCollision(false);
	PickupActor->AttachToComponent(
		AttachTarget,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (const ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(PickupActor))
	{
		PickupActor->SetActorRelativeLocation(
			DeliveryCrate->GetHeldRelativeLocation());
	}

	HeldActor = PickupActor;
	if (bQuickbarCargo && IsValid(QuickbarInventory))
	{
		const int32 AddedSlotIndex = QuickbarInventory->TryAddItem(PickupActor);
		if (AddedSlotIndex != INDEX_NONE)
		{
			QuickbarInventory->SelectSlot(AddedSlotIndex);
		}
	}

	if (ACSUsableToolActor* ToolActor = Cast<ACSUsableToolActor>(HeldActor))
	{
		// 도구는 손에 든 상태와 물리를 정리해 좌클릭 사용 가능 여부를 판단합니다.
		ToolActor->OnEquipped(this);
	}

	// 들고 있는 대상에 맞춰 실제 진열될 상품의 고스트 프리뷰를 생성합니다.
	if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(HeldActor))
	{
		CreatePlacementPreview(ProductActor);
	}
	else if (const ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(HeldActor))
	{
		CreatePlacementPreviewFromCrate(DeliveryCrate);
	}
	else
	{
		DestroyPlacementPreview();
	}

	// 매 프레임 검사하지 않고 상품을 든 직후 한 번만 진열대 안내 외곽선을 갱신합니다.
	RefreshShelfPlacementHighlights();
    return true;
}

bool ACSPlayerCharacter::TryStoreHeldItem(
	ACSFreshnessSafeZoneActor* StoragePoint,
	const UPrimitiveComponent* TargetInteractionComponent)
{
	if (!IsValid(StoragePoint) || !IsValid(HeldActor))
	{
		return false;
	}

	bool bStored = false;
	if (ACSDeliveryCrateActor* HeldCrate = Cast<ACSDeliveryCrateActor>(HeldActor))
	{
		// 상자는 조준한 Arrow 슬롯의 상호작용 박스가 있으면 그 위치에, 아니면 다음 빈 위치에 적재합니다.
		bStored = TargetInteractionComponent != nullptr
			? StoragePoint->TryStoreCrateInSlot(HeldCrate, TargetInteractionComponent)
			: StoragePoint->TryStoreCrate(HeldCrate);
	}
	else if (ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor))
	{
		// 낱개 상품은 LooseProductStorageZone 내부의 다음 자동 격자 위치에 적재됩니다.
		bStored = StoragePoint->TryStoreLooseProduct(HeldProduct);
	}

	if (!bStored)
	{
		return false;
	}

	// 창고가 액터를 부착하고 신선도 상태를 갱신한 뒤에만 플레이어 손과 진열 프리뷰 참조를 비웁니다.
	RemoveHeldItemFromQuickbar();
	HeldActor = nullptr;
	DestroyPlacementPreview();
	ClearShelfPlacementHighlights();
	return true;
}

bool ACSPlayerCharacter::TryTakeStoredCrate(
	ACSFreshnessSafeZoneActor* StoragePoint,
	const UPrimitiveComponent* TargetInteractionComponent)
{
	if (!IsValid(StoragePoint) || IsValid(HeldActor))
	{
		return false;
	}

	ACSDeliveryCrateActor* StoredCrate = nullptr;
	if (!StoragePoint->TryTakeCrateFromSlot(TargetInteractionComponent, StoredCrate))
	{
		return false;
	}

	if (TryPickUp(StoredCrate))
	{
		return true;
	}

	// 예외적으로 손 부착에 실패하면 같은 슬롯으로 되돌려 상자 유실을 막습니다.
	StoragePoint->TryStoreCrateInSlot(StoredCrate, TargetInteractionComponent);
	return false;
}

// ---------------------------------------------------------------------------
// 세이브 / 로드
// ---------------------------------------------------------------------------

void ACSPlayerCharacter::CaptureQuickbarRecord(FCSQuickbarRecord& OutRecord) const
{
	OutRecord = FCSQuickbarRecord();
	if (!IsValid(QuickbarInventory))
	{
		return;
	}

	OutRecord.SelectedSlotIndex = QuickbarInventory->GetSelectedSlotIndex();

	// 빈 칸도 빈 레코드로 기록해 슬롯 수와 선택 번호를 유지
	const int32 SlotCount = QuickbarInventory->GetSlotCount();
	OutRecord.Slots.SetNum(SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const AActor* Item = QuickbarInventory->GetItemAt(SlotIndex);
		FCSQuickbarSlotRecord& SlotRecord = OutRecord.Slots[SlotIndex];
		if (const ACSUsableToolActor* Tool = Cast<ACSUsableToolActor>(Item))
		{
			SlotRecord.ToolClass = FSoftClassPath(Tool->GetClass());
		}
		else if (const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Item))
		{
			SlotRecord.bHasCrate = true;
			Crate->CaptureSaveRecord(SlotRecord.Crate);
		}
	}
}

void ACSPlayerCharacter::RestoreQuickbarRecord(
	const FCSQuickbarRecord& InRecord,
	const UDataTable* FallbackProductTable)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(QuickbarInventory))
	{
		return;
	}

	for (const FCSQuickbarSlotRecord& SlotRecord : InRecord.Slots)
	{
		AActor* RestoredItem = nullptr;
		bool bSpawned = false;
		if (SlotRecord.ToolClass.IsValid())
		{
			UClass* ToolClass = SlotRecord.ToolClass.TryLoadClass<ACSUsableToolActor>();
			if (ToolClass == nullptr)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[CSSave] 퀵바 도구 클래스를 불러오지 못했습니다: %s"),
					*SlotRecord.ToolClass.ToString());
				continue;
			}

			RestoredItem = FindOrSpawnQuickbarTool(ToolClass, bSpawned);
		}
		else if (SlotRecord.bHasCrate)
		{
			RestoredItem = UCSWorldStateSaveHandler::SpawnCrateFromRecord(
				*World,
				SlotRecord.Crate,
				GetActorTransform(),
				FallbackProductTable);
			bSpawned = true;
		}

		if (!IsValid(RestoredItem))
		{
			continue;
		}

		// 이전 항목을 퀵바 보관 위치로 옮겨 손을 비운 뒤 다음 항목을 첫 빈 칸에 추가
		StoreHeldQuickbarItem();
		if (!TryPickUp(RestoredItem) && bSpawned)
		{
			// 퀵바가 가득 찼거나 장착 조건이 맞지 않으면 새로 만든 액터를 제거
			RestoredItem->Destroy();
		}
	}

	// 저장 시점의 선택 슬롯을 복원하고 같은 번호로 선택이 생략된 경우에도 장착 상태를 맞춤
	const int32 MaxSlotIndex = FMath::Max(0, QuickbarInventory->GetSlotCount() - 1);
	QuickbarInventory->SelectSlot(FMath::Clamp(InRecord.SelectedSlotIndex, 0, MaxSlotIndex));
	EquipSelectedQuickbarTool();
	UpdateQuickbarHUD();
}

bool ACSPlayerCharacter::IsQuickbarItem(const AActor* ItemActor) const
{
	return IsValid(QuickbarInventory) && QuickbarInventory->ContainsItem(ItemActor);
}

ACSUsableToolActor* ACSPlayerCharacter::FindOrSpawnQuickbarTool(UClass* ToolClass, bool& bOutSpawned)
{
	bOutSpawned = false;
	UWorld* World = GetWorld();
	if (World == nullptr || ToolClass == nullptr)
	{
		return nullptr;
	}

	// 마법봉은 시설 거치대가 레벨 시작 시 생성하므로 퀵바에 없는 기존 액터를 재사용
	if (ToolClass->IsChildOf(ACSMagicWandActor::StaticClass()))
	{
		for (TActorIterator<ACSMagicWandActor> It(World); It; ++It)
		{
			ACSMagicWandActor* MagicWand = *It;
			if (IsValid(MagicWand) &&
				MagicWand->GetClass() == ToolClass &&
				!IsQuickbarItem(MagicWand) &&
				Cast<ACSPlayerCharacter>(MagicWand->GetAttachParentActor()) == nullptr)
			{
				return MagicWand;
			}
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACSUsableToolActor* ToolActor = World->SpawnActor<ACSUsableToolActor>(
		ToolClass,
		GetActorTransform(),
		SpawnParameters);
	bOutSpawned = IsValid(ToolActor);
	return ToolActor;
}

void ACSPlayerCharacter::UpdateInteractionHUD()
{
	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	if (!IsValid(StorePlayerController) || FirstPersonCamera == nullptr)
	{
		return;
	}

	// 매 프레임 조준 대상을 새로 확인하므로 이전 대상의 안내가 남지 않게 먼저 숨깁니다.
	StorePlayerController->ClearInteractionContext();

	// 계산·발주 UI를 조작하는 동안에는 뒤에 있는 액터의 상호작용 안내를 다시 표시하지 않습니다.
	if (StorePlayerController->IsGameplayUIInputLocked())
	{
		return;
	}

	const FVector TraceStart = FirstPersonCamera->GetComponentLocation();
	const FVector TraceEnd =
		TraceStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;
	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (!GetWorld()->LineTraceSingleByChannel(
        HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
    {
		RestoreGlobalShelfPlacementHighlights();
        return;
    }

	AActor* TargetActor = HitResult.GetActor();
	if (const ACSStoreEmployeeCharacter* Employee = Cast<ACSStoreEmployeeCharacter>(TargetActor))
		if (AActor* Inventory = Employee->GetCarriedInventoryForPlayer()) TargetActor = Inventory;
	if (!IsValid(TargetActor))
	{
		return;
	}
	// 엔딩 재료를 든 경우에도 다른 엔딩 재료의 줍기 안내를 표시합니다.
	if (IsValid(HeldActor) && HeldActor->IsA<ACSEndingItemActor>() &&
		!TargetActor->IsA<ACSEndingWizardActor>() && !TargetActor->IsA<ACSEndingVendorActor>() &&
		!TargetActor->IsA<ACSEndingDisplayActor>() && !TargetActor->IsA<ACSEndingItemActor>())
	{
		return;
	}
	if (ACSEndingDisplayActor* Display = Cast<ACSEndingDisplayActor>(TargetActor))
	{
		AimedEndingDisplay = Display;
		Display->UpdatePlacementPreview(this, HitResult.GetComponent());
		// 슬롯별 안내에는 입력 키 또는 배치 불가 사유가 이미 포함되어 있습니다.
		StorePlayerController->SetInteractionPrompt(Display->GetSlotPrompt(this, HitResult.GetComponent()), FLinearColor::White);
		return;
	}
	// 상품을 든 상태에서는 이미 진열된 상품을 조준해도, 그 상품이 속한 진열대를 계속 배치 대상으로 사용합니다.
	// 이 처리가 없으면 상품 메시와 선반 메시 사이에서 Trace 대상이 바뀌며 안내와 프리뷰가 깜빡일 수 있습니다.
	if (IsValid(HeldActor))
	{
		if (const ACSProductPickupActor* HitProduct = Cast<ACSProductPickupActor>(TargetActor))
		{
			if (AActor* ParentShelf = HitProduct->GetAttachParentActor();
				IsValid(ParentShelf) && ParentShelf->Implements<UCSShelfPlaceable>())
			{
				TargetActor = ParentShelf;
			}
		}
	}

	if (const ACSFreshnessSafeZoneActor* StoragePoint = Cast<ACSFreshnessSafeZoneActor>(TargetActor))
	{
		int32 UnusedSlotIndex = INDEX_NONE;
		if (StoragePoint->TryGetCrateStorageSlotIndex(HitResult.GetComponent(), UnusedSlotIndex))
		{
			const ACSDeliveryCrateActor* HeldCrate = Cast<ACSDeliveryCrateActor>(HeldActor);
			const bool bCanStore = IsValid(HeldCrate) &&
				StoragePoint->CanStoreCrateInSlot(HeldCrate, HitResult.GetComponent());
			ACSDeliveryCrateActor* StoredCrate = nullptr;
			StoragePoint->TryGetStoredCrateFromSlot(HitResult.GetComponent(), StoredCrate);
			const FText Prompt = IsValid(HeldActor)
				? (HeldCrate != nullptr
					? (bCanStore
						? FText::FromString(TEXT("[Q] 이 상자 슬롯에 보관"))
						: FText::FromString(TEXT("이 상자는 이 창고에 보관할 수 없습니다.")))
					: FText::FromString(TEXT("상자 슬롯에는 배송 상자만 보관할 수 있습니다.")))
				: IsValid(StoredCrate)
					? FText::Format(
						NSLOCTEXT("Interaction", "TakeStoredCrate", "[E] {0} 상자 꺼내기"),
						StoredCrate->GetProductDisplayName())
					: FText::FromString(TEXT("[E] 상자 꺼내기"));
			StorePlayerController->SetInteractionPrompt(
				Prompt,
				IsValid(HeldActor) && !bCanStore ? FLinearColor(1.0f, 0.25f, 0.25f, 1.0f) : FLinearColor::White);
			if (!IsValid(HeldActor) && IsValid(StoredCrate))
			{
				// 슬롯 상자를 조준할 때는 실제 상자를 꺼내지 않고도 이름·가격·유통기한·신선도를 모두 확인할 수 있습니다.
				ShowProductPrice(StoredCrate->GetProductRow(), StoredCrate->GetRemainingQuantity());
				ShowProductShelfLife(StoredCrate->GetProductRuntimeState());
				ShowProductFreshness(StoredCrate->GetProductRow(), StoredCrate->GetCurrentFreshness());
				if (StoredCrate->IsReservedByEmployee())
				{
					ShowEmployeeReservationWarning(FText::FromString(TEXT("직원이 상자를 운반할 예정입니다.")));
				}
			}
			return;
		}

		if (StoragePoint->IsLooseProductDepositComponent(HitResult.GetComponent()))
		{
			const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor);
			const bool bCanStore = IsValid(HeldProduct) && StoragePoint->CanStoreLooseProduct(HeldProduct);
			const FText Prompt = HeldProduct != nullptr
				? (bCanStore
					? FText::FromString(TEXT("[Q] 낱개 상품 보관"))
					: FText::FromString(TEXT("이 상품은 이 창고에 보관할 수 없습니다.")))
				: FText::FromString(TEXT("낱개 상품을 든 상태에서 보관할 수 있습니다."));
			StorePlayerController->SetInteractionPrompt(
				Prompt,
				HeldProduct != nullptr && !bCanStore ? FLinearColor(1.0f, 0.25f, 0.25f, 1.0f) : FLinearColor::White);
			return;
		}
	}

	if (const ACSCartActor* Cart = Cast<ACSCartActor>(TargetActor))
	{
		if (!IsValid(HeldActor) && !Cart->IsBeingPushed())
		{
			StorePlayerController->SetInteractionPrompt(
				NSLOCTEXT("Interaction", "PushCartPrompt", "[F] 수레 잡기"),
				FLinearColor::White);
		}
		return;
	}

	if (const ACSShelfActor* Shelf = Cast<ACSShelfActor>(TargetActor))
	{
		StorePlayerController->SetInteractionPrompts(
			GetShelfInteractionPrompts(Shelf, HitResult.ImpactPoint, Cast<ACSProductPickupActor>(HitResult.GetActor())));
		return;
	}

	// 마법진에 수락한 주문의 상품이 모두 모였을 때만 E 상호작용을 제공합니다.
	// 준비 전 상태는 단순 안내이므로 [E] 접두어 없이 표시하고, 상호작용도 실행되지 않습니다.
	if (const ACSDeliveryMagicCircleActor* MagicCircle = Cast<ACSDeliveryMagicCircleActor>(TargetActor);
		IsValid(MagicCircle) && !MagicCircle->HasRequiredProducts())
	{
		StorePlayerController->SetInteractionPrompt(
			MagicCircle->GetUnavailableInteractionText(),
			FLinearColor(1.0f, 0.8f, 0.3f, 1.0f));
		return;
	}

	// 완료된 기록 지점은 다시 상호작용할 수 없으므로 안내 문구만 표시합니다.
	if (const ACSJournalDiscoveryPoint* DiscoveryPoint = Cast<ACSJournalDiscoveryPoint>(TargetActor);
		IsValid(DiscoveryPoint) && DiscoveryPoint->IsDiscoveryComplete())
	{
		StorePlayerController->SetInteractionPrompt(
			DiscoveryPoint->GetInteractionText_Implementation(), FLinearColor::White);
		return;
	}

	if (!TargetActor->Implements<UCSInteractable>())
	{
		return;
	}

	if (!IsValid(HeldActor))
	{
		if (const ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(TargetActor);
			IsValid(DeliveryCrate) && DeliveryCrate->IsReservedByEmployee())
		{
			ShowEmployeeReservationWarning(FText::FromString(TEXT("직원이 상자를 운반할 예정입니다.")));
		}
		else if (const ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(TargetActor);
			IsValid(ProductActor) && ProductActor->IsReservedByEmployee())
		{
			ShowEmployeeReservationWarning(FText::FromString(TEXT("직원이 상품을 운반할 예정입니다.")));
		}
	}

	// 계산대는 조준 상태를 유지한 채 직원 구역 안에 있는지도 함께 안내합니다.
	const ACSCheckoutCounterActor* CheckoutCounter = Cast<ACSCheckoutCounterActor>(TargetActor);
	const FText InteractionText = IsValid(CheckoutCounter)
		? CheckoutCounter->GetInteractionTextForPlayer(this)
		: ICSInteractable::Execute_GetInteractionText(TargetActor);
	if (InteractionText.IsEmpty())
	{
		return;
	}

	StorePlayerController->SetInteractionPrompt(
		Cast<ACSProductPickupActor>(TargetActor) ? GetProductCollectionPrompt(Cast<ACSProductPickupActor>(TargetActor)) : FText::Format(
			NSLOCTEXT("Interaction", "UsePrompt", "[E] {0}"),
			InteractionText),
		FLinearColor::White);

	FDataTableRowHandle ProductRow;
	if (TryGetProductRowFromActor(TargetActor, ProductRow))
	{
		const ACSDeliveryCrateActor* DeliveryCrate =
			Cast<ACSDeliveryCrateActor>(TargetActor);
		ShowProductPrice(
			ProductRow,
			IsValid(DeliveryCrate) ? DeliveryCrate->GetRemainingQuantity() : INDEX_NONE);
	}

	if (const ACSProductPickupActor* ProductActor =
		Cast<ACSProductPickupActor>(TargetActor))
	{
		ShowProductShelfLife(ProductActor->GetProductRuntimeState());
		ShowProductFreshness(
			ProductActor->GetProductRow(),
			ProductActor->GetCurrentFreshness());
	}
	else if (const ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(TargetActor))
	{
		ShowProductShelfLife(DeliveryCrate->GetProductRuntimeState());
		ShowProductFreshness(
			DeliveryCrate->GetProductRow(),
			DeliveryCrate->GetCurrentFreshness());
	}
}

void ACSPlayerCharacter::UpdateBuildModeHUD()
{
	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	if (!IsValid(StorePlayerController))
	{
		return;
	}

	StorePlayerController->ClearInteractionContext();
	if (IsValid(HeldActor))
	{
		if (HeldActor->Implements<UCSBuildableFacility>())
		{
			// HUD에 표시될 건축 불가 사유를 결정하는 조건문
			FText Prompt = NSLOCTEXT("Interaction", "BuildPlaceFacilityPrompt", "[Q] 시설 배치");
			FLinearColor PromptColor = FLinearColor::White;
			if (bHasHeldBuildablePlacementOverlap)
			{
				Prompt = NSLOCTEXT("Interaction", "BuildBlockedByOverlap",
					"다른 시설과 겹칩니다");
				PromptColor = FLinearColor::Red;
			}
			else if (bHasHeldBuildableAccessZoneConflict)
			{
				Prompt = NSLOCTEXT("Interaction", "BuildBlockedByAccessZone",
					"이동·대기 구역을 막습니다");
				PromptColor = FLinearColor::Red;
			}
			else if (!bIsHeldBuildableInsidePlacementArea)
			{
				Prompt = NSLOCTEXT("Interaction", "BuildOutsideArea",
					"매장 밖에는 놓을 수 없습니다");
				PromptColor = FLinearColor::Red;
			}
			else if (!bIsHeldBuildableAccessZoneInsideArea)
			{
				Prompt = NSLOCTEXT("Interaction", "BuildZoneOutsideArea",
					"손님이 설 자리가 매장 밖으로 나갑니다");
				PromptColor = FLinearColor::Red;
			}

			StorePlayerController->SetInteractionPrompt(Prompt, PromptColor);
		}
		return;
	}

	if (FirstPersonCamera == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	const FVector TraceStart = FirstPersonCamera->GetComponentLocation();
	const FVector TraceEnd =
		TraceStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;
	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (!GetWorld()->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams))
	{
		return;
	}

	AActor* TargetActor = HitResult.GetActor();
	if (IsValid(TargetActor) && TargetActor->Implements<UCSBuildableFacility>())
	{
		StorePlayerController->SetInteractionPrompt(
			NSLOCTEXT("Interaction", "BuildPickUpFacilityPrompt", "[E] 들기"),
			FLinearColor::White);
	}
}

void ACSPlayerCharacter::ConfigureBuildPlacementPreview(AActor* FacilityActor)
{
	ClearBuildPlacementPreview();
	if (!IsValid(FacilityActor) || !IsValid(BuildPlacementPreviewMesh))
	{
		return;
	}

	TArray<UStaticMeshComponent*> SourceMeshes;
	TSet<AActor*> VisitedActors;
	TFunction<void(AActor*)> CollectChildActorMeshes;
	CollectChildActorMeshes =
		[&SourceMeshes, &VisitedActors, &CollectChildActorMeshes](AActor* SourceActor)
	{
		if (!IsValid(SourceActor) || VisitedActors.Contains(SourceActor))
		{
			return;
		}

		VisitedActors.Add(SourceActor);
		if (UStaticMeshComponent* RootMesh =
			Cast<UStaticMeshComponent>(SourceActor->GetRootComponent());
			IsValid(RootMesh) && IsValid(RootMesh->GetStaticMesh()))
		{
			SourceMeshes.Add(RootMesh);
		}

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		SourceActor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
		for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
		{
			if (IsValid(StaticMeshComponent) &&
				IsValid(StaticMeshComponent->GetStaticMesh()) &&
				!SourceMeshes.Contains(StaticMeshComponent))
			{
				SourceMeshes.Add(StaticMeshComponent);
			}
		}

		TArray<UChildActorComponent*> ChildActorComponents;
		SourceActor->GetComponents<UChildActorComponent>(ChildActorComponents);
		for (const UChildActorComponent* ChildActorComponent : ChildActorComponents)
		{
			if (IsValid(ChildActorComponent))
			{
				CollectChildActorMeshes(ChildActorComponent->GetChildActor());
			}
		}
	};
	CollectChildActorMeshes(FacilityActor);
	if (SourceMeshes.IsEmpty())
	{
		return;
	}

	const FTransform FacilityTransform = FacilityActor->GetActorTransform();
	for (int32 MeshIndex = 0; MeshIndex < SourceMeshes.Num(); ++MeshIndex)
	{
		UStaticMeshComponent* SourceMesh = SourceMeshes[MeshIndex];
		UStaticMeshComponent* PreviewMesh = BuildPlacementPreviewMesh;
		if (MeshIndex > 0)
		{
			PreviewMesh = NewObject<UStaticMeshComponent>(
				this,
				NAME_None,
				RF_Transient);
			if (!IsValid(PreviewMesh))
			{
				ClearBuildPlacementPreview();
				return;
			}

			PreviewMesh->SetupAttachment(GetRootComponent());
			AddInstanceComponent(PreviewMesh);
			PreviewMesh->RegisterComponent();
		}

		PreviewMesh->SetAbsolute(true, true, true);
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewMesh->SetGenerateOverlapEvents(false);
		PreviewMesh->SetCanEverAffectNavigation(false);
		PreviewMesh->SetCastShadow(false);
		PreviewMesh->SetHiddenInGame(true);
		PreviewMesh->SetVisibility(false);
		PreviewMesh->SetStaticMesh(SourceMesh->GetStaticMesh());
		PreviewMesh->EmptyOverrideMaterials();
		for (int32 MaterialIndex = 0;
			MaterialIndex < SourceMesh->GetNumMaterials();
			++MaterialIndex)
		{
			PreviewMesh->SetMaterial(
				MaterialIndex,
				SourceMesh->GetMaterial(MaterialIndex));
		}

		HeldBuildableSourceMeshes.Add(SourceMesh);
		BuildPlacementPreviewMeshes.Add(PreviewMesh);
		HeldBuildableSourceRelativeTransforms.Add(
			SourceMesh->GetComponentTransform().GetRelativeTransform(FacilityTransform));
	}

	HeldBuildableSourceMesh = HeldBuildableSourceMeshes[0];

	// 들고 있는 시설의 예정 접근 영역을 시각화하기 위해 프리뷰 매시를 생성
	// 색상 스왑이나 위치 갱신 로직은 몸통 배열과 같이 처리되도록 함
	if (IsValid(AccessZoneDisplayMesh))
	{
		TArray<UBoxComponent*> AccessZoneBoxes;
		CollectBuildPlacementBoxesByTag(
			FacilityActor,
			BuildPlacementAccessZoneTag,
			AccessZoneBoxes);
		for (const UBoxComponent* AccessZoneBox : AccessZoneBoxes)
		{
			// 박스의 가로(X)나 세로(Y) 크기가 0에 가까우면 유효하지 않은 영역이므로 무시함
			const FVector ZoneUnscaledExtent = AccessZoneBox->GetUnscaledBoxExtent();
			if (ZoneUnscaledExtent.X <= UE_KINDA_SMALL_NUMBER ||
				ZoneUnscaledExtent.Y <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			// 접근 영역을 표시할 컴포넌트를 동적으로 생성
			UStaticMeshComponent* ZonePlane = NewObject<UStaticMeshComponent>(
				this,
				NAME_None,
				RF_Transient);
			if (!IsValid(ZonePlane))
			{
				continue;
			}

			// 이 평면은 오직 시각적인 표시 용도
			// 따라서 물리 충돌, 오버랩 이벤트, 네비게이션 메시(손님 길찾기) 영향, 그림자 등을 전부 꺼서 게임 플레이에 개입X
			ZonePlane->SetupAttachment(GetRootComponent());
			AddInstanceComponent(ZonePlane);
			ZonePlane->RegisterComponent();
			ZonePlane->SetAbsolute(true, true, true);
			ZonePlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ZonePlane->SetGenerateOverlapEvents(false);
			ZonePlane->SetCanEverAffectNavigation(false);
			ZonePlane->SetCastShadow(false);
			ZonePlane->SetHiddenInGame(true);
			ZonePlane->SetVisibility(false);
			ZonePlane->SetStaticMesh(AccessZoneDisplayMesh);

			// 진열대를 집어 든 상태라 카메라 Pitch 각도가 섞여 있음
			// 바닥 평면이 뜨거나 땅을 파고드는 것을 막기 위해, 현재 위치를 그대로 고정하지 않고
			// 진열대 중심점으로부터의 상대 트랜스폼 및 원본 크기만 배열에 저장함
			// 이후 Tick마다 저장한 값들을 꺼내서 바닥에 깔리도록 위치를 새로 계산하는 데 사용함
			HeldAccessZonePreviewMeshes.Add(ZonePlane);
			HeldAccessZoneBoxRelativeTransforms.Add(
				AccessZoneBox->GetComponentTransform().GetRelativeTransform(FacilityTransform));
			HeldAccessZoneBoxUnscaledExtents.Add(ZoneUnscaledExtent);
		}
	}
}

void ACSPlayerCharacter::ClearBuildPlacementPreview()
{
	bHasBuildPlacementPreview = false;
	bCanPlaceHeldBuildableFacility = false;
	bIsHeldBuildableInsidePlacementArea = true;
	bHasHeldBuildablePlacementOverlap = false;
	bHasHeldBuildableAccessZoneConflict = false;//상태 플래그 초기화
	bIsHeldBuildableAccessZoneInsideArea = true;//상태 플래그 초기화
	CachedBlockedAccessZoneBoxes.Reset(); //저장된 값 비우기
	ClearInstalledAccessZoneDisplays(); //영역 렌더링 끄기
	CachedBuildPlacementLocation = FVector::ZeroVector;
	CachedBuildPlacementRotation = FRotator::ZeroRotator;
	HeldBuildableSourceMesh = nullptr;
	HeldBuildableSourceMeshes.Reset();
	HeldBuildableSourceRelativeTransforms.Reset();
	for (UStaticMeshComponent* PreviewMesh : BuildPlacementPreviewMeshes)
	{
		if (IsValid(PreviewMesh) && PreviewMesh != BuildPlacementPreviewMesh)
		{
			PreviewMesh->DestroyComponent();
		}
	}
	BuildPlacementPreviewMeshes.Reset();
	// 프리뷰가 종료될 때 월드에 잔상이 남지 않도록 직접 파괴해 줍니다.
	for (UStaticMeshComponent* ZonePlane : HeldAccessZonePreviewMeshes)
	{
		if (IsValid(ZonePlane))
		{
			ZonePlane->DestroyComponent();
		}
	}
	// 평면을 파괴한 후, 위치 계산에 사용하던 관련 배열들도 비워 다음 배치를 준비
	HeldAccessZonePreviewMeshes.Reset();
	HeldAccessZoneBoxRelativeTransforms.Reset();
	HeldAccessZoneBoxUnscaledExtents.Reset();
	if (IsValid(BuildPlacementPreviewMesh))
	{
		BuildPlacementPreviewMesh->SetHiddenInGame(true);
		BuildPlacementPreviewMesh->SetVisibility(false);
		BuildPlacementPreviewMesh->SetStaticMesh(nullptr);
		BuildPlacementPreviewMesh->EmptyOverrideMaterials();
	}
}

void ACSPlayerCharacter::ApplyBuildPlacementPreviewMaterial(bool bIsPlacementValid)
{
	UMaterialInterface* PreviewMaterial = bIsPlacementValid
		? BuildPlacementValidMaterial.Get()
		: BuildPlacementInvalidMaterial.Get();
	for (int32 MeshIndex = 0;
		MeshIndex < BuildPlacementPreviewMeshes.Num() &&
		MeshIndex < HeldBuildableSourceMeshes.Num();
		++MeshIndex)
	{
		UStaticMeshComponent* PreviewMesh = BuildPlacementPreviewMeshes[MeshIndex];
		UStaticMeshComponent* SourceMesh = HeldBuildableSourceMeshes[MeshIndex];
		if (!IsValid(PreviewMesh) || !IsValid(SourceMesh))
		{
			continue;
		}

		PreviewMesh->EmptyOverrideMaterials();
		for (int32 MaterialIndex = 0;
			MaterialIndex < SourceMesh->GetNumMaterials();
			++MaterialIndex)
		{
			PreviewMesh->SetMaterial(
				MaterialIndex,
				IsValid(PreviewMaterial)
					? PreviewMaterial
					: SourceMesh->GetMaterial(MaterialIndex));
		}
	}

	// 배열은 스냅 계산 오류를 막기 위해 분리했지만, 시각적인 색상 피드백은 몸통과 똑같이 받아야 함.
	// 따라서 몸통 프리뷰의 색상이 바뀔 때, 별도로 보관 중인 바닥 영역 평면들도 꺼내어 같은 색상으로 덮어씌워 줌
	if (IsValid(PreviewMaterial))
	{
		for (UStaticMeshComponent* ZonePlane : HeldAccessZonePreviewMeshes)
		{
			if (IsValid(ZonePlane))
			{
				ZonePlane->SetMaterial(0, PreviewMaterial);
			}
		}
	}
}

void ACSPlayerCharacter::SetBuildPlacementPreviewVisible(bool bIsVisible)
{
	for (UStaticMeshComponent* PreviewMesh : BuildPlacementPreviewMeshes)
	{
		if (IsValid(PreviewMesh))
		{
			PreviewMesh->SetHiddenInGame(!bIsVisible);
			PreviewMesh->SetVisibility(bIsVisible);
		}
	}
	// 배열이 몸통 프리뷰와 분리되어 있으므로, 화면 가시성을 켜고 끄는 명령도 이 반복문을 통해 바닥 평면들에 별도로 전달해야 함
	// 몸통 프리뷰가 화면에서 숨겨지거나 다시 나타날 때, 바닥 영역 평면도 같이 숨겨지거나 나타나게 동기화.
	for (UStaticMeshComponent* ZonePlane : HeldAccessZonePreviewMeshes)
	{
		if (IsValid(ZonePlane))
		{
			ZonePlane->SetHiddenInGame(!bIsVisible);
			ZonePlane->SetVisibility(bIsVisible);
		}
	}
}

void ACSPlayerCharacter::UpdateBuildPreviewComponentTransforms(
	const FVector& FacilityLocation,
	const FRotator& FacilityRotation) const
{
	const FVector FacilityScale = IsValid(HeldActor)
		? HeldActor->GetActorScale3D()
		: FVector::OneVector;
	const FTransform FacilityTransform(
		FacilityRotation,
		FacilityLocation,
		FacilityScale);
	for (int32 MeshIndex = 0;
		MeshIndex < BuildPlacementPreviewMeshes.Num() &&
		MeshIndex < HeldBuildableSourceRelativeTransforms.Num();
		++MeshIndex)
	{
		UStaticMeshComponent* PreviewMesh = BuildPlacementPreviewMeshes[MeshIndex];
		if (!IsValid(PreviewMesh))
		{
			continue;
		}

		PreviewMesh->SetWorldTransform(
			HeldBuildableSourceRelativeTransforms[MeshIndex] * FacilityTransform);
		PreviewMesh->UpdateBounds();
	}

	// 예정 접근 영역 평면은 배치 스냅 계산(CalculateCombinedBuildPreviewBounds)에
	// 들어가면 안 되므로 별도 배열로 두고 여기서만 같이 옮깁니다.
	for (int32 ZoneIndex = 0;
		ZoneIndex < HeldAccessZonePreviewMeshes.Num() &&
		ZoneIndex < HeldAccessZoneBoxRelativeTransforms.Num() &&
		ZoneIndex < HeldAccessZoneBoxUnscaledExtents.Num();
		++ZoneIndex)
	{
		UStaticMeshComponent* ZonePlane = HeldAccessZonePreviewMeshes[ZoneIndex];
		if (!IsValid(ZonePlane))
		{
			continue;
		}

		// 판정이 쓰는 TryCalculateProspectiveBox와 같은 계산입니다.
		// 여기서 배치 예정 트랜스폼으로 새로 풀어야 손에 든 자세가 섞이지 않습니다.
		const FTransform ZoneBoxTransform =
			HeldAccessZoneBoxRelativeTransforms[ZoneIndex] * FacilityTransform;
		const FVector ZoneScaledExtent =
			HeldAccessZoneBoxUnscaledExtents[ZoneIndex] *
			ZoneBoxTransform.GetScale3D().GetAbs();
		// 배치,크기 보정은 설치된 시설 표시와 같은 함수를 써서 두 표시가 어긋나지 않게 함
		FVector ZonePlaneLocation;
		FRotator ZonePlaneRotation;
		FVector ZonePlaneScale;
		if (!TryCalculateAccessZoneDisplayTransform(
				ZoneBoxTransform.GetLocation(),
				ZoneScaledExtent,
				ZoneBoxTransform.Rotator().Yaw,
				ZonePlaneLocation,
				ZonePlaneRotation,
				ZonePlaneScale))
		{
			continue;
		}

		ZonePlane->SetWorldLocationAndRotation(
			ZonePlaneLocation,
			ZonePlaneRotation.Quaternion());
		ZonePlane->SetWorldScale3D(ZonePlaneScale);
		ZonePlane->UpdateBounds();
	}
}

bool ACSPlayerCharacter::CalculateCombinedBuildPreviewBounds(FBox& OutBounds) const
{
	OutBounds = FBox(EForceInit::ForceInit);
	bool bHasValidBounds = false;
	for (const UStaticMeshComponent* PreviewMesh : BuildPlacementPreviewMeshes)
	{
		if (!IsValid(PreviewMesh) || !IsValid(PreviewMesh->GetStaticMesh()))
		{
			continue;
		}

		const FBoxSphereBounds ComponentBounds = PreviewMesh->CalcBounds(
			PreviewMesh->GetComponentTransform());
		if (ComponentBounds.Origin.ContainsNaN() ||
			ComponentBounds.BoxExtent.ContainsNaN())
		{
			continue;
		}

		const FBox ComponentBox = ComponentBounds.GetBox();
		if (!ComponentBox.IsValid)
		{
			continue;
		}

		OutBounds += ComponentBox;
		bHasValidBounds = true;
	}

	return bHasValidBounds && OutBounds.IsValid;
}

bool ACSPlayerCharacter::AlignBuildPreviewToFloor(
	FVector& InOutFacilityLocation,
	const FRotator& FacilityRotation,
	float FloorZ) const
{
	if (InOutFacilityLocation.ContainsNaN() ||
		FacilityRotation.ContainsNaN() ||
		!FMath::IsFinite(FloorZ))
	{
		return false;
	}

	UpdateBuildPreviewComponentTransforms(InOutFacilityLocation, FacilityRotation);
	FBox CombinedBounds;
	if (!CalculateCombinedBuildPreviewBounds(CombinedBounds) ||
		!FMath::IsFinite(CombinedBounds.Min.Z))
	{
		return false;
	}

	InOutFacilityLocation.Z += FloorZ - CombinedBounds.Min.Z;
	UpdateBuildPreviewComponentTransforms(InOutFacilityLocation, FacilityRotation);
	return !InOutFacilityLocation.ContainsNaN();
}

bool ACSPlayerCharacter::TryCalculateNearestBuildPreviewDistance(
	const FVector& SurfacePoint,
	const FVector& SurfaceNormal,
	float& OutNearestDistance) const
{
	OutNearestDistance = TNumericLimits<float>::Max();
	bool bCalculatedDistance = false;
	for (const UStaticMeshComponent* PreviewMesh : BuildPlacementPreviewMeshes)
	{
		if (!IsValid(PreviewMesh))
		{
			continue;
		}

		const UStaticMesh* PreviewStaticMesh = PreviewMesh->GetStaticMesh();
		if (!IsValid(PreviewStaticMesh))
		{
			continue;
		}

		const FBox LocalBounds = PreviewStaticMesh->GetBoundingBox();
		if (!LocalBounds.IsValid)
		{
			continue;
		}

		const FTransform PreviewTransform = PreviewMesh->GetComponentTransform();
		for (int32 XIndex = 0; XIndex < 2; ++XIndex)
		{
			for (int32 YIndex = 0; YIndex < 2; ++YIndex)
			{
				for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
				{
					const FVector LocalCorner(
						XIndex == 0 ? LocalBounds.Min.X : LocalBounds.Max.X,
						YIndex == 0 ? LocalBounds.Min.Y : LocalBounds.Max.Y,
						ZIndex == 0 ? LocalBounds.Min.Z : LocalBounds.Max.Z);
					const FVector WorldCorner =
						PreviewTransform.TransformPosition(LocalCorner);
					const float CornerDistance = FVector::DotProduct(
						WorldCorner - SurfacePoint,
						SurfaceNormal);
					if (!WorldCorner.ContainsNaN() && FMath::IsFinite(CornerDistance))
					{
						OutNearestDistance = FMath::Min(
							OutNearestDistance,
							CornerDistance);
						bCalculatedDistance = true;
					}
				}
			}
		}
	}

	return bCalculatedDistance;
}

void ACSPlayerCharacter::CacheHeldBuildableActorStates(AActor* FacilityActor)
{
	HeldBuildableStateActors.Reset();
	HeldBuildableHiddenStates.Reset();
	HeldBuildableCollisionStates.Reset();
	if (!IsValid(FacilityActor))
	{
		return;
	}

	TArray<AActor*> ActorsToCache;
	ActorsToCache.Add(FacilityActor);
	TArray<AActor*> AttachedActors;
	FacilityActor->GetAttachedActors(AttachedActors, true, true);
	ActorsToCache.Append(AttachedActors);
	for (AActor* Actor : ActorsToCache)
	{
		if (!IsValid(Actor) || HeldBuildableStateActors.Contains(Actor))
		{
			continue;
		}

		HeldBuildableStateActors.Add(Actor);
		HeldBuildableHiddenStates.Add(Actor->IsHidden() ? 1 : 0);
		HeldBuildableCollisionStates.Add(Actor->GetActorEnableCollision() ? 1 : 0);
	}
}

void ACSPlayerCharacter::HideHeldBuildableActors()
{
	for (AActor* Actor : HeldBuildableStateActors)
	{
		if (IsValid(Actor))
		{
			Actor->SetActorHiddenInGame(true);
			Actor->SetActorEnableCollision(false);
		}
	}
}

void ACSPlayerCharacter::RestoreHeldBuildableActorStates()
{
	for (int32 ActorIndex = 0;
		ActorIndex < HeldBuildableStateActors.Num() &&
		ActorIndex < HeldBuildableHiddenStates.Num();
		++ActorIndex)
	{
		if (AActor* Actor = HeldBuildableStateActors[ActorIndex]; IsValid(Actor))
		{
			Actor->SetActorHiddenInGame(HeldBuildableHiddenStates[ActorIndex] != 0);
		}
	}

	for (int32 ActorIndex = 0;
		ActorIndex < HeldBuildableStateActors.Num() &&
		ActorIndex < HeldBuildableCollisionStates.Num();
		++ActorIndex)
	{
		if (AActor* Actor = HeldBuildableStateActors[ActorIndex]; IsValid(Actor))
		{
			Actor->SetActorEnableCollision(HeldBuildableCollisionStates[ActorIndex] != 0);
		}
	}

	HeldBuildableStateActors.Reset();
	HeldBuildableHiddenStates.Reset();
	HeldBuildableCollisionStates.Reset();
}

void ACSPlayerCharacter::RefreshBuildPlacementAreaCache()
{
	CachedBuildPlacementAreas.Reset();
	CachedAccessZoneFacilities.Reset();
	bBuildPlacementAreaCacheInitialized = true;
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	// Tick 판정에서는 월드를 다시 순회하지 않도록 건축모드 진입 시 tagged Box만 캐시합니다.
	for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
	{
		AActor* AreaOwner = *ActorIterator;
		if (!IsValid(AreaOwner))
		{
			continue;
		}

		// 접근 영역 판정 대상도 같은 순회에서 모읍니다.
		// 박스는 판정 시점에 현재 트랜스폼을 읽으므로 시설이 움직여도 재수집이 필요 없습니다.
		if (AreaOwner->Implements<UCSBuildableFacility>() || AreaOwner->IsA<ACSEmployeeWaitingArea>())
		{
			CachedAccessZoneFacilities.Add(AreaOwner);
		}

		TArray<UBoxComponent*> BoxComponents;
		AreaOwner->GetComponents<UBoxComponent>(BoxComponents);
		for (UBoxComponent* BoxComponent : BoxComponents)
		{
			if (IsValid(BoxComponent) &&
				BoxComponent->ComponentHasTag(BuildPlacementAreaTag))
			{
				CachedBuildPlacementAreas.Add(BoxComponent);
			}
		}
	}
}

void ACSPlayerCharacter::CollectBuildPlacementBoxesByTag(
	AActor* FacilityActor,
	const FName& BoxTag,
	TArray<UBoxComponent*>& OutBoxes) const
{
	OutBoxes.Reset();
	if (!IsValid(FacilityActor))
	{
		return;
	}

	// 시설 본체뿐 아니라 부착된 자식 액터의 박스까지 봅니다.
	// 계산대처럼 시설 BP가 BeginPlay에서 스폰해 소켓에 붙이는 부속 액터는
	// GetComponents로는 절대 잡히지 않아서, 태그를 달아도 판정에 걸리지 않습니다.
	TArray<AActor*> SearchActors;
	SearchActors.Add(FacilityActor);
	FacilityActor->GetAttachedActors(SearchActors, false, true);

	TArray<UBoxComponent*> BoxComponents;
	for (AActor* SearchActor : SearchActors)
	{
		if (!IsValid(SearchActor))
		{
			continue;
		}

		SearchActor->GetComponents<UBoxComponent>(BoxComponents);
		for (UBoxComponent* BoxComponent : BoxComponents)
		{
			if (IsValid(BoxComponent) &&
				BoxComponent->ComponentHasTag(BoxTag))
			{
				OutBoxes.Add(BoxComponent);
			}
		}
	}
}

bool ACSPlayerCharacter::HasUnusableQueueSlotPlacement(
	AActor* FacilityActor,
	const FVector& FacilityLocation,
	const FRotator& FacilityRotation) const
{
	if (!IsValid(FacilityActor) ||
		FacilityLocation.ContainsNaN() ||
		FacilityRotation.ContainsNaN())
	{
		return false;
	}

	// 규칙 A·B는 상대가 시설일 때만 돌기 때문에 벽·기둥·계단은 검사하지 않습니다.
	// 그래서 대기줄이 벽을 향해 뻗는 배치도 그냥 통과합니다.
	// 규칙 D: 대기줄 슬롯이 전부 설 수 있는 자리일 때만 배치를 허용합니다.
	TArray<AActor*> SearchActors;
	SearchActors.Add(FacilityActor);
	FacilityActor->GetAttachedActors(SearchActors, false, true);

	const FTransform ProspectiveFacilityTransform(
		FacilityRotation,
		FacilityLocation,
		FacilityActor->GetActorScale3D());

	for (const AActor* SearchActor : SearchActors)
	{
		const ACSCheckoutCounterActor* Counter = Cast<ACSCheckoutCounterActor>(SearchActor);
		if (!IsValid(Counter))
		{
			continue;
		}

		// 계산대는 시설에 붙어 있으므로, 시설이 배치될 자리 기준으로 계산대 트랜스폼을 다시 풀어 줍니다.
		const FTransform RelativeCounterTransform =
			Counter->GetActorTransform().GetRelativeTransform(FacilityActor->GetActorTransform());
		if (!Counter->AreQueueSlotsUsableAt(
			RelativeCounterTransform * ProspectiveFacilityTransform))
		{
			return true;
		}
	}

	return false;
}

bool ACSPlayerCharacter::IsBuildPlacementInsideCachedAreas(
	AActor* FacilityActor,
	const TArray<UBoxComponent*>& CollisionBoxes,
	const FVector& FacilityLocation,
	const FRotator& FacilityRotation) const
{
	if (!IsValid(FacilityActor) || CollisionBoxes.IsEmpty())
	{
		return true;
	}

	TArray<UBoxComponent*> ValidAreaBoxes;
	for (const TWeakObjectPtr<UBoxComponent>& AreaBoxWeak : CachedBuildPlacementAreas)
	{
		if (UBoxComponent* AreaBox = AreaBoxWeak.Get();
			IsValid(AreaBox) && AreaBox->ComponentHasTag(BuildPlacementAreaTag))
		{
			ValidAreaBoxes.Add(AreaBox);
		}
	}
	// Area가 아직 설정되지 않은 프로젝트 상태에서는 기존 건축 기능을 막지 않습니다.
	if (ValidAreaBoxes.IsEmpty())
	{
		return true;
	}

	auto IsPointInsideAreaUnion = [&ValidAreaBoxes](const FVector& WorldPoint)
	{
		for (const UBoxComponent* AreaBox : ValidAreaBoxes)
		{
			const FTransform AreaTransform = AreaBox->GetComponentTransform();
			const FVector AreaScale = AreaTransform.GetScale3D().GetAbs();
			if (AreaScale.GetMin() <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector LocalPoint =
				AreaTransform.InverseTransformPosition(WorldPoint);
			const FVector AreaExtent = AreaBox->GetUnscaledBoxExtent();
			if (!LocalPoint.ContainsNaN() &&
				FMath::Abs(LocalPoint.X) <= AreaExtent.X + KINDA_SMALL_NUMBER &&
				FMath::Abs(LocalPoint.Y) <= AreaExtent.Y + KINDA_SMALL_NUMBER &&
				FMath::Abs(LocalPoint.Z) <= AreaExtent.Z + KINDA_SMALL_NUMBER)
			{
				return true;
			}
		}
		return false;
	};

	for (const UBoxComponent* CollisionBox : CollisionBoxes)
	{
		FTransform ProspectiveBoxTransform;
		FVector ScaledExtent;
		// 기준은 반드시 시설입니다. 박스 주인을 기준으로 삼으면 부속 액터가 들고 있는 박스가
		// 시설 원점으로 끌려와, 실제 위치와 다른 자리를 검사하게 됩니다.
		if (!TryCalculateProspectiveBox(
				CollisionBox,
				FacilityActor,
				FacilityLocation,
				FacilityRotation,
				ProspectiveBoxTransform,
				ScaledExtent))
		{
			continue;
		}

		const FVector Center = ProspectiveBoxTransform.GetLocation();
		const FQuat Rotation = ProspectiveBoxTransform.GetRotation();
		if (!IsPointInsideAreaUnion(Center))
		{
			return false;
		}

		for (int32 XSign = -1; XSign <= 1; XSign += 2)
		{
			for (int32 YSign = -1; YSign <= 1; YSign += 2)
			{
				for (int32 ZSign = -1; ZSign <= 1; ZSign += 2)
				{
					const FVector CornerOffset(
						ScaledExtent.X * XSign,
						ScaledExtent.Y * YSign,
						ScaledExtent.Z * ZSign);
					if (!IsPointInsideAreaUnion(
						Center + Rotation.RotateVector(CornerOffset)))
					{
						return false;
					}
				}
			}
		}
	}

	// 잘못된 크기의 tagged Box는 건너뛰어 기존 배치를 fail-open으로 유지합니다.
	return true;
}

bool ACSPlayerCharacter::HasBuildPlacementCollisionOverlap(
	AActor* FacilityActor,
	const TArray<UBoxComponent*>& CollisionBoxes,
	const FVector& FacilityLocation,
	const FRotator& FacilityRotation) const
{
	if (!IsValid(FacilityActor) || CollisionBoxes.IsEmpty() || GetWorld() == nullptr)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BuildPlacementCollision), false);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(FacilityActor);
	TArray<AActor*> AttachedActors;
	FacilityActor->GetAttachedActors(AttachedActors, true, true);
	QueryParams.AddIgnoredActors(AttachedActors);
	const FCollisionObjectQueryParams ObjectQueryParams(
		FCollisionObjectQueryParams::AllObjects);

	for (const UBoxComponent* CollisionBox : CollisionBoxes)
	{
		FTransform ProspectiveBoxTransform;
		FVector ScaledExtent;
		if (!TryCalculateProspectiveBox(
				CollisionBox,
				FacilityActor,
				FacilityLocation,
				FacilityRotation,
				ProspectiveBoxTransform,
				ScaledExtent))
		{
			continue;
		}

		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(
			Overlaps,
			ProspectiveBoxTransform.GetLocation(),
			ProspectiveBoxTransform.GetRotation(),
			ObjectQueryParams,
			FCollisionShape::MakeBox(ScaledExtent),
			QueryParams);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UBoxComponent* OtherBox =
				Cast<UBoxComponent>(Overlap.Component.Get());
			AActor* OtherFacility = IsValid(OtherBox)
				? OtherBox->GetOwner()
				: nullptr;
			if (IsValid(OtherBox) &&
				OtherBox->ComponentHasTag(BuildPlacementCollisionTag) &&
				IsValid(OtherFacility) &&
				OtherFacility != FacilityActor &&
				OtherFacility->Implements<UCSBuildableFacility>())
			{
				return true;
			}
		}
	}

	return false;
}

bool ACSPlayerCharacter::HasBuildPlacementAccessZoneConflict(
	AActor* FacilityActor,
	const TArray<UBoxComponent*>& MyBodyBoxes,
	const TArray<UBoxComponent*>& MyZoneBoxes,
	const FVector& FacilityLocation,
	const FRotator& FacilityRotation,
	TArray<TWeakObjectPtr<UBoxComponent>>& OutBlockedZoneBoxes) const
{
	// 이전 프레임에서 밟고 있던 상대방 영역 명단을 초기화
	OutBlockedZoneBoxes.Reset();
	if (!IsValid(FacilityActor))
	{
		return false;
	}

	// 배치 예정 위치로 선택된 진열대 몸통과 영역 박스들을 가상으로 미리 옮겨서 계산할 준비
	TArray<TPair<FTransform, FVector>> MyBodyProspective;
	TArray<TPair<FTransform, FVector>> MyZoneProspective;
	auto BuildProspective = [&](
		const TArray<UBoxComponent*>& SourceBoxes,
		TArray<TPair<FTransform, FVector>>& OutProspective)
	{
		for (const UBoxComponent* SourceBox : SourceBoxes)
		{
			FTransform BoxTransform;
			FVector ScaledExtent;
			if (TryCalculateProspectiveBox(
					SourceBox,
					FacilityActor,
					FacilityLocation,
					FacilityRotation,
					BoxTransform,
					ScaledExtent))
			{
				OutProspective.Emplace(BoxTransform, ScaledExtent);
			}
		}
	};
	BuildProspective(MyBodyBoxes, MyBodyProspective);
	BuildProspective(MyZoneBoxes, MyZoneProspective);
	if (MyBodyProspective.IsEmpty() && MyZoneProspective.IsEmpty())
	{
		return false;
	}

	bool bHasConflict = false;
	TArray<UBoxComponent*> OtherBodyBoxes;
	TArray<UBoxComponent*> OtherZoneBoxes;
	// 맵에 존재하는 다른 진열대들과 가상 박스들을 비교
	for (const TWeakObjectPtr<AActor>& OtherWeak : CachedAccessZoneFacilities)
	{
		AActor* Other = OtherWeak.Get();
		if (!IsValid(Other) ||
			Other == FacilityActor ||
			(!Other->IsA<ACSEmployeeWaitingArea>() && !ICSBuildableFacility::Execute_IsFacilityInstalled(Other)))
		{
			continue;
		}
		// 멀리 있는 시설은 계산에서 제외해 Tick 비용을 묶어 둡니다.
		if (!Other->IsA<ACSEmployeeWaitingArea>() && FVector::DistSquared2D(Other->GetActorLocation(), FacilityLocation) >
			FMath::Square(AccessZoneQueryRange))
		{
			continue;
		}

		CollectBuildPlacementBoxesByTag(Other, BuildPlacementCollisionTag, OtherBodyBoxes);
		CollectBuildPlacementBoxesByTag(Other, BuildPlacementAccessZoneTag, OtherZoneBoxes);

		// 규칙 A: 내 몸통이 남의 접근 영역을 침범했는지?
		// 막힌 영역을 전부 지정한 색으로 변경해야 하므로 중간에 return하지 않습니다.
		for (UBoxComponent* OtherZone : OtherZoneBoxes)
		{
			for (const TPair<FTransform, FVector>& MyBody : MyBodyProspective)
			{
				if (DoesBoxOverlapBox2D(
						MyBody.Key,
						MyBody.Value,
						OtherZone->GetComponentTransform(),
						OtherZone->GetScaledBoxExtent()))
				{
					OutBlockedZoneBoxes.AddUnique(OtherZone);
					bHasConflict = true;
					break;
				}
			}
		}

		// 규칙 B: 내 접근 영역이 남의 몸통에 막히는가
		for (const UBoxComponent* OtherBody : OtherBodyBoxes)
		{
			for (const TPair<FTransform, FVector>& MyZone : MyZoneProspective)
			{
				if (DoesBoxOverlapBox2D(
						MyZone.Key,
						MyZone.Value,
						OtherBody->GetComponentTransform(),
						OtherBody->GetScaledBoxExtent()))
				{
					return true;
				}
			}
		}
	}

	return bHasConflict;
}

bool ACSPlayerCharacter::TryCalculateBuildSnapTransform(
	const FVector& BasePlacementLocation,
	const FRotator& ManualRotation,
	FVector& OutSnappedLocation,
	FRotator& OutSnappedRotation) const
{
	OutSnappedLocation = BasePlacementLocation;
	OutSnappedRotation = ManualRotation;
	if (BuildPlacementPreviewMeshes.IsEmpty() ||
		!IsValid(HeldActor) ||
		GetWorld() == nullptr ||
		BasePlacementLocation.ContainsNaN() ||
		ManualRotation.ContainsNaN())
	{
		return false;
	}

	FBox PreviewBounds;
	if (!CalculateCombinedBuildPreviewBounds(PreviewBounds))
	{
		return false;
	}

	const FVector BoundsOrigin = PreviewBounds.GetCenter();
	const FVector BoundsExtent = PreviewBounds.GetExtent();
	const float SearchDistance = FMath::Max(0.0f, WallSnapSearchDistance);
	const float HorizontalSearchRadius = FVector2D(
		BoundsExtent.X,
		BoundsExtent.Y).Size();
	if (BoundsOrigin.ContainsNaN() ||
		BoundsExtent.ContainsNaN() ||
		BoundsExtent.GetMin() <= 0.0f ||
		!FMath::IsFinite(HorizontalSearchRadius) ||
		SearchDistance <= 0.0f)
	{
		return false;
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(HeldActor);
	TArray<AActor*> AttachedActors;
	HeldActor->GetAttachedActors(AttachedActors, true, true);
	QueryParams.AddIgnoredActors(AttachedActors);

	constexpr int32 WallTraceDirectionCount = 8;
	constexpr float MaximumWallNormalZ = 0.25f;
	constexpr float WallGap = 2.0f;
	constexpr float MovementTieTolerance = 0.1f;
	float BestMovementDistance = TNumericLimits<float>::Max();
	float BestYawDifference = TNumericLimits<float>::Max();
	bool bFoundWallSnap = false;
	for (int32 DirectionIndex = 0;
		DirectionIndex < WallTraceDirectionCount;
		++DirectionIndex)
	{
		const float TraceYaw =
			ManualRotation.Yaw + DirectionIndex * 45.0f;
		const FVector TraceDirection = FRotator(0.0f, TraceYaw, 0.0f).Vector();
		const FVector TraceEnd =
			BoundsOrigin + TraceDirection * (HorizontalSearchRadius + SearchDistance);
		TArray<FHitResult> SurfaceHits;
		if (!GetWorld()->LineTraceMultiByObjectType(
			SurfaceHits,
			BoundsOrigin,
			TraceEnd,
			ObjectQueryParams,
			QueryParams))
		{
			continue;
		}

		for (const FHitResult& SurfaceHit : SurfaceHits)
		{
			if (!SurfaceHit.IsValidBlockingHit())
			{
				continue;
			}

			AActor* SurfaceActor = SurfaceHit.GetActor();
			UPrimitiveComponent* SurfaceComponent = SurfaceHit.GetComponent();
			if (SurfaceActor == HeldActor || AttachedActors.Contains(SurfaceActor))
			{
				continue;
			}
			if (IsValid(SurfaceActor) &&
				(Cast<ACSProductPickupActor>(SurfaceActor) != nullptr ||
				Cast<ACSDeliveryCrateActor>(SurfaceActor) != nullptr ||
				SurfaceActor->Implements<UCSUsableTool>()))
			{
				continue;
			}

			const bool bIsBuildableFacility =
				IsValid(SurfaceActor) &&
				SurfaceActor->Implements<UCSBuildableFacility>();
			const bool bIsInstalledFacility =
				bIsBuildableFacility &&
				ICSBuildableFacility::Execute_IsFacilityInstalled(SurfaceActor);
			const bool bIsWorldStaticSurface =
				!bIsBuildableFacility &&
				IsValid(SurfaceComponent) &&
				SurfaceComponent->GetCollisionObjectType() == ECC_WorldStatic;
			if (!bIsInstalledFacility && !bIsWorldStaticSurface)
			{
				continue;
			}

			const FVector ImpactNormal = SurfaceHit.ImpactNormal;
			if (ImpactNormal.ContainsNaN() ||
				SurfaceHit.ImpactPoint.ContainsNaN() ||
				FMath::Abs(ImpactNormal.Z) > MaximumWallNormalZ)
			{
				continue;
			}

			FVector HorizontalSurfaceNormal(ImpactNormal.X, ImpactNormal.Y, 0.0f);
			if (!HorizontalSurfaceNormal.Normalize())
			{
				continue;
			}
			if (FVector::DotProduct(
				BoundsOrigin - SurfaceHit.ImpactPoint,
				HorizontalSurfaceNormal) < 0.0f)
			{
				HorizontalSurfaceNormal *= -1.0f;
			}

			const float SurfaceNormalYaw = HorizontalSurfaceNormal.Rotation().Yaw;
			float AutoYaw = SurfaceNormalYaw;
			float YawDifference = TNumericLimits<float>::Max();
			for (int32 YawCandidateIndex = 0;
				YawCandidateIndex < 4;
				++YawCandidateIndex)
			{
				const float CandidateYaw = FRotator::NormalizeAxis(
					SurfaceNormalYaw + YawCandidateIndex * 90.0f);
				const float CandidateYawDifference = FMath::Abs(
					FMath::FindDeltaAngleDegrees(ManualRotation.Yaw, CandidateYaw));
				if (CandidateYawDifference < YawDifference)
				{
					AutoYaw = CandidateYaw;
					YawDifference = CandidateYawDifference;
				}
			}

			FRotator CandidateRotation = ManualRotation;
			CandidateRotation.Yaw = AutoYaw;
			UpdateBuildPreviewComponentTransforms(
				BasePlacementLocation,
				CandidateRotation);

			float NearestDistance = TNumericLimits<float>::Max();
			const bool bCalculatedNearestDistance =
				TryCalculateNearestBuildPreviewDistance(
					SurfaceHit.ImpactPoint,
					HorizontalSurfaceNormal,
					NearestDistance);

			if (!bCalculatedNearestDistance)
			{
				FBox CandidateBounds;
				if (!CalculateCombinedBuildPreviewBounds(CandidateBounds))
				{
					continue;
				}

				const FVector CandidateBoundsOrigin = CandidateBounds.GetCenter();
				const FVector CandidateBoundsExtent = CandidateBounds.GetExtent();
				const float SupportRadius =
					FMath::Abs(HorizontalSurfaceNormal.X) * CandidateBoundsExtent.X +
					FMath::Abs(HorizontalSurfaceNormal.Y) * CandidateBoundsExtent.Y;
				NearestDistance = FVector::DotProduct(
					CandidateBoundsOrigin - SurfaceHit.ImpactPoint,
					HorizontalSurfaceNormal) - SupportRadius;
			}

			const float MoveDistance = WallGap - NearestDistance;
			const float MovementDistance = FMath::Abs(MoveDistance);
			if (!FMath::IsFinite(NearestDistance) ||
				!FMath::IsFinite(MoveDistance) ||
				NearestDistance > SearchDistance)
			{
				continue;
			}

			const bool bHasBetterMovement =
				MovementDistance < BestMovementDistance - MovementTieTolerance;
			const bool bHasEqualMovement =
				FMath::Abs(MovementDistance - BestMovementDistance) <=
				MovementTieTolerance;
			const bool bHasBetterYaw =
				YawDifference < BestYawDifference - KINDA_SMALL_NUMBER;
			if (!bHasBetterMovement && !(bHasEqualMovement && bHasBetterYaw))
			{
				continue;
			}

			FVector CandidateLocation =
				BasePlacementLocation + HorizontalSurfaceNormal * MoveDistance;
			CandidateLocation.Z = BasePlacementLocation.Z;
			if (CandidateLocation.ContainsNaN())
			{
				continue;
			}

			BestMovementDistance = MovementDistance;
			BestYawDifference = YawDifference;
			OutSnappedLocation = CandidateLocation;
			OutSnappedRotation = CandidateRotation;
			bFoundWallSnap = true;
		}
	}

	UpdateBuildPreviewComponentTransforms(
		OutSnappedLocation,
		OutSnappedRotation);
	return bFoundWallSnap;
}

void ACSPlayerCharacter::UpdateBuildPlacementPreview()
{
	// 계산 실패 시 이전 프레임의 불가 사유가 Q 입력에 남지 않도록 먼저 초기화합니다.
	bIsHeldBuildableInsidePlacementArea = true;
	bHasHeldBuildablePlacementOverlap = false;
	bHasHeldBuildableAccessZoneConflict = false;
	bIsHeldBuildableAccessZoneInsideArea = true;
	CachedBlockedAccessZoneBoxes.Reset();
	if (!IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>() ||
		BuildPlacementPreviewMeshes.IsEmpty() ||
		HeldBuildableSourceMeshes.IsEmpty() ||
		BuildPlacementPreviewMeshes.Num() != HeldBuildableSourceMeshes.Num() ||
		BuildPlacementPreviewMeshes.Num() != HeldBuildableSourceRelativeTransforms.Num())
	{
		ClearBuildPlacementPreview();
		return;
	}
	for (int32 MeshIndex = 0; MeshIndex < BuildPlacementPreviewMeshes.Num(); ++MeshIndex)
	{
		if (!IsValid(BuildPlacementPreviewMeshes[MeshIndex]) ||
			!IsValid(HeldBuildableSourceMeshes[MeshIndex]))
		{
			ClearBuildPlacementPreview();
			return;
		}
	}

	FVector PlacementLocation;
	bool bIsSurfaceValid = false;
	if (!TryCalculateBuildPlacementLocation(
		HeldActor,
		PlacementLocation,
		bIsSurfaceValid))
	{
		bHasBuildPlacementPreview = false;
		bCanPlaceHeldBuildableFacility = false;
		SetBuildPlacementPreviewVisible(false);
		return;
	}

	const float FloorZ = PlacementLocation.Z;
	if (!AlignBuildPreviewToFloor(
		PlacementLocation,
		HeldBuildableFacilityRotation,
		FloorZ))
	{
		bHasBuildPlacementPreview = false;
		bCanPlaceHeldBuildableFacility = false;
		SetBuildPlacementPreviewVisible(false);
		return;
	}

	FVector FinalPlacementLocation = PlacementLocation;
	FRotator FinalPlacementRotation = HeldBuildableFacilityRotation;
	TryCalculateBuildSnapTransform(
		PlacementLocation,
		HeldBuildableFacilityRotation,
		FinalPlacementLocation,
		FinalPlacementRotation);

	if (!AlignBuildPreviewToFloor(
		FinalPlacementLocation,
		FinalPlacementRotation,
		FloorZ))
	{
		bHasBuildPlacementPreview = false;
		bCanPlaceHeldBuildableFacility = false;
		SetBuildPlacementPreviewVisible(false);
		return;
	}

	CachedBuildPlacementLocation = FinalPlacementLocation;
	CachedBuildPlacementRotation = FinalPlacementRotation;
	bHasBuildPlacementPreview = true;
	if (!bBuildPlacementAreaCacheInitialized)
	{
		RefreshBuildPlacementAreaCache();
	}

	TArray<UBoxComponent*> BuildPlacementCollisionBoxes;
	TArray<UBoxComponent*> BuildPlacementAccessZoneBoxes;
	CollectBuildPlacementBoxesByTag(
		HeldActor,
		BuildPlacementCollisionTag,
		BuildPlacementCollisionBoxes);
	CollectBuildPlacementBoxesByTag(
		HeldActor,
		BuildPlacementAccessZoneTag,
		BuildPlacementAccessZoneBoxes);
	const bool bIsInsideBuildPlacementArea =
		IsBuildPlacementInsideCachedAreas(
			HeldActor,
			BuildPlacementCollisionBoxes,
			CachedBuildPlacementLocation,
			CachedBuildPlacementRotation);
	const bool bHasFacilityOverlap =
		HasBuildPlacementCollisionOverlap(
			HeldActor,
			BuildPlacementCollisionBoxes,
			CachedBuildPlacementLocation,
			CachedBuildPlacementRotation);
	// 규칙 A / B. 막힌 영역 목록은 설치된 시설의 영역 표시가 그대로 읽어 갑니다.
	const bool bHasZoneConflict =
		HasBuildPlacementAccessZoneConflict(
			HeldActor,
			BuildPlacementCollisionBoxes,
			BuildPlacementAccessZoneBoxes,
			CachedBuildPlacementLocation,
			CachedBuildPlacementRotation,
			CachedBlockedAccessZoneBoxes);
	// 규칙 C. 몸통과 같은 함수를 배열만 바꿔 호출합니다.
	const bool bIsZoneInsideArea =
		IsBuildPlacementInsideCachedAreas(
			HeldActor,
			BuildPlacementAccessZoneBoxes,
			CachedBuildPlacementLocation,
			CachedBuildPlacementRotation);
	// 규칙 D. 대기줄이 벽이나 다른 시설 쪽으로 뻗는 배치를 막습니다.
	const bool bHasUnusableQueueSlots =
		HasUnusableQueueSlotPlacement(
			HeldActor,
			CachedBuildPlacementLocation,
			CachedBuildPlacementRotation);
	bIsHeldBuildableInsidePlacementArea = bIsInsideBuildPlacementArea;
	bHasHeldBuildablePlacementOverlap = bHasFacilityOverlap;
	bHasHeldBuildableAccessZoneConflict = bHasZoneConflict;
	bIsHeldBuildableAccessZoneInsideArea = bIsZoneInsideArea;
	bHasHeldBuildableUnusableQueueSlots = bHasUnusableQueueSlots;
	// 신규 세 조건만 롤아웃 스위치로 묶습니다. 기존 조건은 검증된 동작이라 넣지 않습니다.
	const bool bPassesAccessZoneRules =
		!bBlockPlacementOnAccessZoneRules ||
		(!bHasZoneConflict && bIsZoneInsideArea && !bHasUnusableQueueSlots);
	bCanPlaceHeldBuildableFacility =
		bIsSurfaceValid &&
		bIsInsideBuildPlacementArea &&
		!bHasFacilityOverlap &&
		bPassesAccessZoneRules;
	ApplyBuildPlacementPreviewMaterial(bCanPlaceHeldBuildableFacility);
	SetBuildPlacementPreviewVisible(true);
}

void ACSPlayerCharacter::UpdatePlacementPreview()
{
	// 수레가 아닌 대상을 다시 조준하면 이전 수레의 상자 고스트를 먼저 제거합니다.
	ClearCartPlacementPreview();
	// 창고가 아닌 대상을 다시 조준하면 이전 창고 상자 고스트도 함께 제거합니다.
	ClearStorageCratePlacementPreview();

	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	if (IsValid(StorePlayerController))
	{
		// 조준 위치가 바뀌었을 때 이전 안내가 남지 않도록 먼저 숨깁니다.
		StorePlayerController->ClearInteractionContext();
	}

    if (!IsValid(HeldActor) || FirstPersonCamera == nullptr)
    {
		SetPlacementPreviewVisible(false);
        return;
    }

	if (const ACSProductPickupActor* ProductActor =
		Cast<ACSProductPickupActor>(HeldActor))
	{
		ShowProductFreshness(
			ProductActor->GetProductRow(),
			ProductActor->GetCurrentFreshness());
	}
	else if (const ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(HeldActor))
	{
		ShowProductFreshness(
			DeliveryCrate->GetProductRow(),
			DeliveryCrate->GetCurrentFreshness());
	}

	// 진열대에 조준하지 않는 동안에는 이전 위치의 고스트가 남지 않도록 숨깁니다.
	SetPlacementPreviewVisible(false);

    const FVector TraceStart = FirstPersonCamera->GetComponentLocation();
    const FVector TraceEnd =
        TraceStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;

    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);
    QueryParams.AddIgnoredActor(HeldActor);

    if (!GetWorld()->LineTraceSingleByChannel(
        HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
    {
        return;
    }

    AActor* TargetActor = HitResult.GetActor();
    if (!IsValid(TargetActor))
    {
		RestoreGlobalShelfPlacementHighlights();
        return;
    }

	// 진열된 상품을 향한 Trace도 해당 상품의 부모 진열대로 해석하여 프리뷰가 안정적으로 유지되게 합니다.
	if (const ACSProductPickupActor* HitProduct = Cast<ACSProductPickupActor>(TargetActor))
	{
		if (AActor* ParentShelf = HitProduct->GetAttachParentActor();
			IsValid(ParentShelf) && ParentShelf->Implements<UCSShelfPlaceable>())
		{
			TargetActor = ParentShelf;
		}
	}
	if (!TargetActor->Implements<UCSShelfPlaceable>())
	{
		// 첫 번째 Trace가 상품·장식 메시를 맞춘 경우에도, 뒤쪽의 실제 진열대를 찾아
		// 빈 슬롯 경고와 빨간 프리뷰가 프레임마다 사라지지 않도록 합니다.
		TArray<FHitResult> AllAimHits;
		if (GetWorld()->LineTraceMultiByChannel(
			AllAimHits, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
		{
			for (const FHitResult& AimHit : AllAimHits)
			{
				AActor* CandidateActor = AimHit.GetActor();
				if (const ACSProductPickupActor* CandidateProduct = Cast<ACSProductPickupActor>(CandidateActor))
				{
					CandidateActor = CandidateProduct->GetAttachParentActor();
				}
				if (IsValid(CandidateActor) &&
					(CandidateActor->Implements<UCSShelfPlaceable>() ||
						Cast<ACSDeliveryMagicCircleActor>(CandidateActor) != nullptr))
				{
					TargetActor = CandidateActor;
					break;
				}
			}
		}
	}

	if (ACSFreshnessSafeZoneActor* StoragePoint = Cast<ACSFreshnessSafeZoneActor>(TargetActor))
	{
		RestoreGlobalShelfPlacementHighlights();
		int32 UnusedSlotIndex = INDEX_NONE;
		if (const ACSDeliveryCrateActor* HeldCrate = Cast<ACSDeliveryCrateActor>(HeldActor);
			HeldCrate != nullptr &&
			StoragePoint->TryGetCrateStorageSlotIndex(HitResult.GetComponent(), UnusedSlotIndex))
		{
			const bool bCanStore = StoragePoint->CanStoreCrateInSlot(HeldCrate, HitResult.GetComponent());
			FTransform SlotWorldTransform;
			if (StoragePoint->TryGetCrateStorageSlotWorldTransform(HitResult.GetComponent(), SlotWorldTransform))
			{
				// 배송 상자는 내부 상품이 아닌 상자 메시 자체를 실제 적재 위치에 고스트로 표시합니다.
				// 타입이 맞지 않거나 슬롯이 사용 중이면 같은 위치에 빨간색으로 표시합니다.
				StoragePoint->ShowCrateStoragePreview(HeldCrate, SlotWorldTransform, bCanStore);
				StorageCratePlacementPreviewTarget = StoragePoint;
			}

			if (IsValid(StorePlayerController))
			{
				StorePlayerController->SetInteractionPrompt(
					bCanStore
						? FText::FromString(TEXT("[Q] 이 상자 슬롯에 보관"))
						: FText::FromString(TEXT("이 상자는 이 창고에 보관할 수 없습니다.")),
					bCanStore ? FLinearColor::White : FLinearColor(1.0f, 0.25f, 0.25f, 1.0f));
			}
			return;
		}

		if (const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor);
			HeldProduct != nullptr &&
			StoragePoint->IsLooseProductDepositComponent(HitResult.GetComponent()))
		{
			const bool bCanStore = StoragePoint->CanStoreLooseProduct(HeldProduct);
			FTransform SlotWorldTransform;
			const bool bHasEmptySlot =
				StoragePoint->TryGetNextLooseProductStorageSlotWorldTransform(SlotWorldTransform);
			if (!bHasEmptySlot)
			{
				// 빈 칸이 없어도 플레이어가 조준한 위치에 빨간 프리뷰를 보여 배치 불가 상태를 명확히 알립니다.
				SlotWorldTransform = FTransform(TargetActor->GetActorRotation(), HitResult.ImpactPoint);
			}
			if (IsValid(PlacementPreviewActor))
			{
				// 낱개 상품은 자동 격자의 다음 빈칸에 기존 상품 고스트를 표시합니다.
				PlacementPreviewActor->SetActorTransform(SlotWorldTransform);
				PlacementPreviewActor->SetPlacementPreviewState(
					bCanStore ? ECSShelfPlacementPreviewState::Valid : ECSShelfPlacementPreviewState::Invalid);
				SetPlacementPreviewVisible(true);
			}

			if (IsValid(StorePlayerController))
			{
				StorePlayerController->SetInteractionPrompt(
					bCanStore
						? FText::FromString(TEXT("[Q] 낱개 상품 보관"))
						: FText::FromString(TEXT("이 상품은 이 창고에 보관할 수 없습니다.")),
					bCanStore ? FLinearColor::White : FLinearColor(1.0f, 0.25f, 0.25f, 1.0f));
			}
			return;
		}
	}

	if (ACSTrashCanActor* TrashCan = Cast<ACSTrashCanActor>(TargetActor))
	{
		RestoreGlobalShelfPlacementHighlights();
		if (TrashCan->CanDisposeItem(HeldActor) && IsValid(StorePlayerController))
		{
			// 제조 실패물은 종류 이름으로 보여 준다
			const ACSTrashActor* HeldTrash = Cast<ACSTrashActor>(HeldActor);
			const FText DisposePrompt = HeldTrash != nullptr
				? FText::Format(NSLOCTEXT("Interaction", "DisposeTrashPromptNamed", "[Q] {0} 버리기"), HeldTrash->GetTrashDisplayName())
				: NSLOCTEXT("Interaction", "DisposeItemPrompt", "[Q] 버리기");
			StorePlayerController->SetInteractionPrompt(
				DisposePrompt,
				FLinearColor::White);
		}
		return;
	}

	// 튜토리얼 쓰레기는 쓰레기통 외 일반 진열대에 배치하지 않습니다.
	if (Cast<ACSTrashActor>(HeldActor) != nullptr)
	{
		RestoreGlobalShelfPlacementHighlights();
		return;
	}

	if (ACSDeliveryMagicCircleActor* MagicCircle = Cast<ACSDeliveryMagicCircleActor>(TargetActor))
	{
		RestoreGlobalShelfPlacementHighlights();
		if (MagicCircle->IsDispatchReady() &&
			(Cast<ACSProductPickupActor>(HeldActor) != nullptr || Cast<ACSDeliveryCrateActor>(HeldActor) != nullptr) &&
			IsValid(PlacementPreviewActor))
		{
			PlacementPreviewActor->SetActorTransform(MagicCircle->GetDeliveryPreviewTransform());
			PlacementPreviewActor->SetPlacementPreviewState(ECSShelfPlacementPreviewState::Valid);
			SetPlacementPreviewVisible(true);
			if (IsValid(StorePlayerController))
			{
				StorePlayerController->SetInteractionPrompt(
					Cast<ACSDeliveryCrateActor>(HeldActor) != nullptr
						? NSLOCTEXT("Interaction", "PlaceCrateProductOnMagicCircle", "[Q] 상자에서 상품 하나 올리기")
						: NSLOCTEXT("Interaction", "PlaceProductOnMagicCircle", "[Q] 마법진에 상품 올리기"),
					FLinearColor::White);
			}
		}
		return;
	}

	if (!TargetActor->Implements<UCSShelfPlaceable>())
	{
		RestoreGlobalShelfPlacementHighlights();
		return;
	}

	AActor* PlacementItem = HeldActor;
	ACSCartActor* TargetCart = Cast<ACSCartActor>(TargetActor);
	if (Cast<ACSDeliveryCrateActor>(HeldActor) != nullptr && TargetCart == nullptr)
	{
		// 배송 상자 자체가 아니라 실제 상품 타입인 프리뷰 액터로 슬롯 적합성을 검사합니다.
		PlacementItem = PlacementPreviewActor;
	}
	if (!IsValid(PlacementItem))
	{
		return;
	}

	// 실패 시에도 진열대가 실제 슬롯 좌표를 반환했는지 구분할 수 있도록 센티널로 초기화합니다.
	const FVector NoSlotPreviewLocation(TNumericLimits<float>::Max());
	FTransform PreviewTransform(FQuat::Identity, NoSlotPreviewLocation);
	const ECSShelfPlacementPreviewState PreviewState =
		ICSShelfPlaceable::Execute_GetPlacementPreviewState(
			TargetActor, PlacementItem, HitResult.ImpactPoint, PreviewTransform);
	if (PreviewState == ECSShelfPlacementPreviewState::Invalid)
    {
		// 진열할 수 없는 경우에도 조준 지점에 빨간 고스트를 남겨,
		// 빈 슬롯 부족이나 대상 불일치를 안내 문구뿐 아니라 시각적으로도 알립니다.
		if (IsValid(PlacementPreviewActor))
		{
			// 가득 찬 단의 경우에는 선반 메시 표면이 아니라 해당 단의 실제 슬롯 위치에 고정합니다.
			const FTransform InvalidPreviewTransform =
				PreviewTransform.GetLocation().Equals(NoSlotPreviewLocation)
					? FTransform(TargetActor->GetActorRotation(), HitResult.ImpactPoint)
					: PreviewTransform;
			PlacementPreviewActor->SetActorTransform(InvalidPreviewTransform);
			PlacementPreviewActor->SetPlacementPreviewState(ECSShelfPlacementPreviewState::Invalid);
			SetPlacementPreviewVisible(true);
		}

		// 진열대는 빈 슬롯 부족·다른 상품 점유·플레이어 지정 설정을 구분해 정확한 이유를 표시합니다.
		if (IsValid(StorePlayerController))
		{
			if (const ACSShelfActor* ShelfActor = Cast<ACSShelfActor>(TargetActor))
			{
				StorePlayerController->SetInteractionPrompt(
					ShelfActor->GetPlayerPlacementFailureText(
						Cast<ACSProductPickupActor>(PlacementItem),
						HitResult.ImpactPoint),
					FLinearColor(1.0f, 0.25f, 0.25f, 1.0f));
			}
			else
			{
				StorePlayerController->ShowPlacementUnavailablePrompt();
			}
		}

		if (ACSShelfActor* ShelfActor = Cast<ACSShelfActor>(TargetActor))
		{
			UpdateAimedShelfPlacementHighlight(
				ShelfActor, static_cast<uint8>(ECSShelfPlacementPreviewState::Invalid));
		}
		return;
	}

	if (TargetCart != nullptr && Cast<ACSDeliveryCrateActor>(HeldActor) != nullptr)
	{
		// 수레에는 상품 고스트가 아닌 배송 상자 자체가 실리므로, 상품 프리뷰는 표시하지 않습니다.
		TargetCart->ShowCratePlacementPreview(
			CastChecked<ACSDeliveryCrateActor>(HeldActor),
			PreviewTransform);
		CartPlacementPreviewTarget = TargetCart;
		if (IsValid(StorePlayerController))
		{
			StorePlayerController->SetInteractionPrompt(
				NSLOCTEXT("Interaction", "LoadCartPrompt", "[Q] 수레에 상자 싣기"),
				FLinearColor::White);
		}
		return;
	}

    if (IsValid(PlacementPreviewActor))
    {
		if (ACSShelfActor* ShelfActor = Cast<ACSShelfActor>(TargetActor))
		{
			UpdateAimedShelfPlacementHighlight(ShelfActor, static_cast<uint8>(PreviewState));
		}

		// 진열대가 반환한 슬롯 Transform을 그대로 적용하여 실제 배치 위치와 일치시킵니다.
		PlacementPreviewActor->SetActorTransform(PreviewTransform);
		PlacementPreviewActor->SetPlacementPreviewState(PreviewState);
		if (IsValid(StorePlayerController))
		{
			StorePlayerController->SetInteractionPrompt(
				NSLOCTEXT("Interaction", "PlacePrompt", "[Q] 진열하기"),
				FLinearColor::White);
		}

		FDataTableRowHandle ProductRow;
		if (TryGetProductRowFromActor(PlacementItem, ProductRow))
		{
			ShowProductPrice(ProductRow);
			if (PreviewState == ECSShelfPlacementPreviewState::Warning)
			{
				ShowStorageWarning(ProductRow);
			}
		}

		if (const ACSProductPickupActor* ProductActor =
			Cast<ACSProductPickupActor>(HeldActor))
		{
			ShowProductShelfLife(ProductActor->GetProductRuntimeState());
		}
		else if (const ACSDeliveryCrateActor* DeliveryCrate =
			Cast<ACSDeliveryCrateActor>(HeldActor))
		{
			ShowProductShelfLife(DeliveryCrate->GetProductRuntimeState());
		}
		SetPlacementPreviewVisible(true);
	}
}

void ACSPlayerCharacter::DropHeldItem()
{
	if (IsBuildMode())
	{
		TryPlaceHeldBuildableFacility();
		return;
	}

	// 수레를 잡고 이동하는 동안에는 상자 적재·집기와 바닥 내려놓기를 모두 막습니다.
	if (IsValid(PushedCart))
	{
		return;
	}

    if (!IsValid(HeldActor) || FirstPersonCamera == nullptr)
    {
        return;
    }

	if (ACSMagicWandActor* MagicWand = Cast<ACSMagicWandActor>(HeldActor);
		IsValid(MagicWand) &&
		IsAimingAtMagicWandHomeDock(MagicWand) &&
		RestoreMagicWandToHomeDock(MagicWand))
	{
		if (IsValid(QuickbarInventory))
		{
			// 거치 성공은 유지하면서 Quickbar에서 제거해 숫자키로 다시 장착되지 않게 합니다.
			QuickbarInventory->RemoveTool(MagicWand);
		}
		PlayPlayerGameplaySFX(this, ECSAudioEvent::WandPlace);
		HeldActor = nullptr;
		DestroyPlacementPreview();
		ClearShelfPlacementHighlights();
		if (ACSStorePlayerController* StorePlayerController =
			Cast<ACSStorePlayerController>(GetController()))
		{
			StorePlayerController->ClearInteractionContext();
		}
		return;
	}

    // 플레이어가 직접 조준한 진열대를 바닥보다 우선 처리합니다.
    const FVector AimStart = FirstPersonCamera->GetComponentLocation();
    const FVector AimEnd =
        AimStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;

    FHitResult AimHitResult;
    FCollisionQueryParams AimQueryParams;
    AimQueryParams.AddIgnoredActor(this);
    AimQueryParams.AddIgnoredActor(HeldActor);

    const bool bHitShelfTarget = GetWorld()->LineTraceSingleByChannel(
        AimHitResult,
        AimStart,
        AimEnd,
        ECC_Visibility,
        AimQueryParams);

	if (bHitShelfTarget)
	{
		AActor* TargetActor = AimHitResult.GetActor();
		if (ACSFreshnessSafeZoneActor* StoragePoint = Cast<ACSFreshnessSafeZoneActor>(TargetActor))
		{
			const UPrimitiveComponent* HitComponent = AimHitResult.GetComponent();
			int32 UnusedSlotIndex = INDEX_NONE;
			if (Cast<ACSDeliveryCrateActor>(HeldActor) != nullptr &&
				StoragePoint->TryGetCrateStorageSlotIndex(HitComponent, UnusedSlotIndex))
			{
				// 창고 적재는 E 상호작용과 분리하여, 손에 든 물건을 내려놓는 Q로만 실행합니다.
				TryStoreHeldItem(StoragePoint, HitComponent);
				return;
			}
			if (Cast<ACSProductPickupActor>(HeldActor) != nullptr &&
				StoragePoint->IsLooseProductDepositComponent(HitComponent))
			{
				TryStoreHeldItem(StoragePoint, HitComponent);
				return;
			}
		}

		// 다른 상품이 선반 앞을 가리고 있어도 실제 배치는 그 상품의 부모 진열대에 시도합니다.
		if (const ACSProductPickupActor* HitProduct = Cast<ACSProductPickupActor>(TargetActor))
		{
			if (AActor* ParentShelf = HitProduct->GetAttachParentActor();
				IsValid(ParentShelf) && ParentShelf->Implements<UCSShelfPlaceable>())
			{
				TargetActor = ParentShelf;
			}
		}
		if (!IsValid(TargetActor) || !TargetActor->Implements<UCSShelfPlaceable>())
		{
			// 프리뷰와 실제 Q 배치가 같은 대상을 사용하도록, 가려진 진열대도 다중 Trace에서 찾아냅니다.
			TArray<FHitResult> AllAimHits;
			if (GetWorld()->LineTraceMultiByChannel(
				AllAimHits, AimStart, AimEnd, ECC_Visibility, AimQueryParams))
			{
				for (const FHitResult& AimHit : AllAimHits)
				{
					AActor* CandidateActor = AimHit.GetActor();
					if (const ACSProductPickupActor* CandidateProduct = Cast<ACSProductPickupActor>(CandidateActor))
					{
						CandidateActor = CandidateProduct->GetAttachParentActor();
					}
					if (IsValid(CandidateActor) &&
						(CandidateActor->Implements<UCSShelfPlaceable>() ||
							Cast<ACSDeliveryMagicCircleActor>(CandidateActor) != nullptr))
					{
						TargetActor = CandidateActor;
						break;
					}
				}
			}
		}
		if (ACSTrashCanActor* TrashCan = Cast<ACSTrashCanActor>(TargetActor);
			IsValid(TrashCan) && TrashCan->TryDisposeItem(HeldActor))
		{
		RemoveHeldItemFromQuickbar();
			HeldActor = nullptr;
			DestroyPlacementPreview();
			ClearShelfPlacementHighlights();
			if (ACSStorePlayerController* StorePlayerController =
				Cast<ACSStorePlayerController>(GetController()))
			{
				StorePlayerController->ClearInteractionContext();
			}
			return;
		}

		if (ACSDeliveryMagicCircleActor* MagicCircle = Cast<ACSDeliveryMagicCircleActor>(TargetActor);
			IsValid(MagicCircle))
		{
			if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(HeldActor);
				IsValid(Product) && MagicCircle->PlaceProduct(Product))
			{
				HeldActor = nullptr;
				DestroyPlacementPreview();
				ClearShelfPlacementHighlights();
				if (ACSStorePlayerController* StorePlayerController = Cast<ACSStorePlayerController>(GetController()))
				{
					StorePlayerController->ClearInteractionContext();
				}
			}
			else if (ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(HeldActor);
				IsValid(DeliveryCrate) && MagicCircle->PlaceOneProductFromCrate(DeliveryCrate))
			{
				if (!IsValid(DeliveryCrate) || DeliveryCrate->GetRemainingQuantity() <= 0)
				{
					RemoveHeldItemFromQuickbar();
					HeldActor = nullptr;
					DestroyPlacementPreview();
					ClearShelfPlacementHighlights();
				}
				else
				{
					RefreshShelfPlacementHighlights();
				}
			}
			return;
		}

		if (IsValid(TargetActor) &&
			Cast<ACSTrashActor>(HeldActor) == nullptr &&
			TargetActor->Implements<UCSShelfPlaceable>())
		{
			if (ACSCartActor* TargetCart = Cast<ACSCartActor>(TargetActor);
				IsValid(TargetCart))
			{
				if (ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(HeldActor);
					IsValid(DeliveryCrate) &&
					ICSShelfPlaceable::Execute_TryPlaceItem(
						TargetCart,
						DeliveryCrate,
						AimHitResult.ImpactPoint))
				{
					RemoveHeldItemFromQuickbar();
					HeldActor = nullptr;
					DestroyPlacementPreview();
					ClearShelfPlacementHighlights();
				}
				return;
			}

			if (ACSDeliveryCrateActor* DeliveryCrate =
				Cast<ACSDeliveryCrateActor>(HeldActor))
			{
				TryPlaceProductFromCrate(
					DeliveryCrate,
					TargetActor,
					AimHitResult.ImpactPoint);
				return;
			}

			if (ICSShelfPlaceable::Execute_TryPlaceItem(
				TargetActor,
				HeldActor,
				AimHitResult.ImpactPoint))
			{
				if (ACSProductPickupActor* PlacedProduct =
					Cast<ACSProductPickupActor>(HeldActor))
				{
					PlacedProduct->SetHeldByPlayerForFreshness(false);
				}
				PlayPlayerGameplaySFX(this, ECSAudioEvent::ProductStock);
            HeldActor = nullptr;
			DestroyPlacementPreview();
            return;
			}
		}
	}

    // 카메라 앞쪽에서 아래로 추적하여 바닥이나 선반 표면을 찾습니다.
    const FVector ForwardDropPoint =
        FirstPersonCamera->GetComponentLocation() +
        FirstPersonCamera->GetForwardVector() * DropDistance;

    const FVector TraceStart = ForwardDropPoint + FVector(0.0f, 0.0f, 50.0f);
    const FVector TraceEnd = ForwardDropPoint - FVector(0.0f, 0.0f, DropTraceDepth);

    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);
    QueryParams.AddIgnoredActor(HeldActor);

    const bool bFoundDropSurface = GetWorld()->LineTraceSingleByChannel(
        HitResult,
        TraceStart,
        TraceEnd,
        ECC_Visibility,
        QueryParams);

    if (!bFoundDropSurface)
    {
        return;
    }

	AActor* ActorToDrop = HeldActor;
	ACSProductPickupActor* ProductToDrop = Cast<ACSProductPickupActor>(ActorToDrop);
	ACSDeliveryCrateActor* CrateToDrop = Cast<ACSDeliveryCrateActor>(ActorToDrop);
	ACSUsableToolActor* ToolToDrop = Cast<ACSUsableToolActor>(ActorToDrop);
	ACSTrashActor* TrashToDrop = Cast<ACSTrashActor>(ActorToDrop);

	// Bounds 계산 전에 물리를 끄고 메시를 정상 SceneRoot/상대 Transform 상태로 맞춥니다.
	if (IsValid(ProductToDrop))
	{
		ProductToDrop->PrepareForPickupPhysics();
	}
	else if (IsValid(CrateToDrop))
	{
		CrateToDrop->PrepareForPickupPhysics();
	}
	else if (IsValid(TrashToDrop))
	{
		// 튜토리얼 쓰레기
		TrashToDrop->PrepareForPickupPhysics();
	}

	const ACSMagicWandActor* MagicWandToDrop = Cast<ACSMagicWandActor>(ActorToDrop);
	const FBox Bounds = IsValid(MagicWandToDrop)
		? MagicWandToDrop->GetUnheldBounds()
		: ActorToDrop->GetComponentsBoundingBox(true);
	const float ActorLocationZ = ActorToDrop->GetActorLocation().Z;
	if (!Bounds.IsValid ||
		!FMath::IsFinite(Bounds.Min.Z) ||
		!FMath::IsFinite(ActorLocationZ))
	{
		return;
	}

	const float BottomOffset = ActorLocationZ - Bounds.Min.Z;
	if (!FMath::IsFinite(BottomOffset))
	{
		return;
	}

	FVector DropLocation = HitResult.ImpactPoint;
	DropLocation.Z = HitResult.ImpactPoint.Z + BottomOffset + 10.0f;
	if (!FMath::IsFinite(DropLocation.Z))
	{
		return;
	}

	HeldActor = nullptr;
	if (IsValid(QuickbarInventory))
	{
		QuickbarInventory->RemoveItem(ActorToDrop);
	}
	DestroyPlacementPreview();

	ActorToDrop->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ActorToDrop->SetActorLocation(DropLocation);
	ActorToDrop->SetActorEnableCollision(true);
	if (IsValid(ToolToDrop))
	{
		ToolToDrop->OnUnequipped();
		ToolToDrop->EnableWorldDropPhysics();
		ToolToDrop->OnReleasedToWorld(false);
	}

	// 최종 안전 위치로 이동한 뒤 실제 월드 Drop 대상의 메시 물리만 활성화합니다.
	if (IsValid(ProductToDrop))
	{
		ProductToDrop->EnableWorldDropPhysics();
	}
	else if (IsValid(CrateToDrop))
	{
		CrateToDrop->EnableWorldDropPhysics();
	}

	if (IsValid(ProductToDrop))
	{
		ProductToDrop->SetHeldByPlayerForFreshness(false);
	}
	else if (IsValid(CrateToDrop))
	{
		CrateToDrop->SetHeldByPlayerForFreshness(false);
	}
	else if (IsValid(TrashToDrop))
	{
		// 튜토리얼 쓰레기
		TrashToDrop->EnableWorldDropPhysics();
		// 떨어지면 메시 다시 보이게
		TrashToDrop->ShowMeshAfterDrop();
	}

}

void ACSPlayerCharacter::HandleDropActionStarted()
{
	ResetDropActionHoldState();
	// 전시대를 조준한 Q는 즉시 배치로 소비하여 실패해도 바닥에 버리거나 던지지 않습니다.
	if (TryPlaceOnEndingDisplay()) return;

	// 건축모드는 기존 Q Started 조작감을 유지하고 홀드 투척 판정을 시작하지 않습니다.
	if (IsBuildMode())
	{
		DropHeldItem();
		return;
	}

	if (IsValid(PushedCart) || !IsValid(HeldActor) || GetWorld() == nullptr)
	{
		return;
	}

	bIsDropActionHeld = true;
	DropActionStartedTimeSeconds = GetWorld()->GetTimeSeconds();
	DropActionHeldActor = HeldActor;
}

void ACSPlayerCharacter::HandleDropActionReleased()
{
	if (!bIsDropActionHeld)
	{
		return;
	}

	UWorld* World = GetWorld();
	AActor* HeldActorAtPress = DropActionHeldActor.Get();
	const float HeldDuration = IsValid(World)
		? FMath::Max(0.0f, World->GetTimeSeconds() - DropActionStartedTimeSeconds)
		: 0.0f;
	const bool bIsSameHeldActor =
		IsValid(HeldActorAtPress) && HeldActor == HeldActorAtPress;
	ResetDropActionHoldState();

	if (!bIsSameHeldActor || IsValid(PushedCart))
	{
		return;
	}
	// Q를 누른 뒤 전시대로 조준을 옮긴 경우에도 투척보다 슬롯 배치를 우선합니다.
	if (TryPlaceOnEndingDisplay()) return;

	if (HeldDuration >= ThrowHoldThreshold)
	{
		ThrowHeldItem();
	}
	else
	{
		DropHeldItem();
	}
}

bool ACSPlayerCharacter::TryPlaceOnEndingDisplay()
{
	if (IsBuildMode() || IsValid(PushedCart) || !IsValid(FirstPersonCamera) ||
		!IsValid(HeldActor) || !HeldActor->IsA<ACSEndingItemActor>()) return false;
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(HeldActor);
	const FVector Start = FirstPersonCamera->GetComponentLocation();
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start,
		Start + FirstPersonCamera->GetForwardVector() * InteractionDistance, ECC_Visibility, Params)) return false;
	ACSEndingDisplayActor* Display = Cast<ACSEndingDisplayActor>(Hit.GetActor());
	if (!Display) return false;
	const ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(GetController());
	if (!StoreController || !StoreController->IsGameplayUIInputLocked()) Display->PlaceInSlot(this, Hit.GetComponent());
	return true;
}

void ACSPlayerCharacter::HandleDropActionCanceled()
{
	ResetDropActionHoldState();
}

void ACSPlayerCharacter::ResetDropActionHoldState()
{
	bIsDropActionHeld = false;
	DropActionStartedTimeSeconds = 0.0f;
	DropActionHeldActor = nullptr;
}

void ACSPlayerCharacter::ThrowHeldItem()
{
	if (IsBuildMode())
	{
		DropHeldItem();
		return;
	}

	if (IsValid(PushedCart) || !IsValid(HeldActor) || !IsValid(FirstPersonCamera))
	{
		return;
	}

	AActor* ActorToThrow = HeldActor;
	ACSProductPickupActor* ProductToThrow = Cast<ACSProductPickupActor>(ActorToThrow);
	ACSDeliveryCrateActor* CrateToThrow = Cast<ACSDeliveryCrateActor>(ActorToThrow);
	ACSTrashActor* TrashToThrow = Cast<ACSTrashActor>(ActorToThrow);
	ACSUsableToolActor* ToolToThrow = Cast<ACSUsableToolActor>(ActorToThrow);
	if (!IsValid(ProductToThrow) && !IsValid(CrateToThrow) &&
		!IsValid(TrashToThrow) && !IsValid(ToolToThrow))
	{
		DropHeldItem();
		return;
	}

	ActorToThrow->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ActorToThrow->SetActorEnableCollision(true);

	if (IsValid(ProductToThrow))
	{
		ProductToThrow->EnableWorldDropPhysics();
		ProductToThrow->SetHeldByPlayerForFreshness(false);
	}
	else if (IsValid(CrateToThrow))
	{
		CrateToThrow->EnableWorldDropPhysics();
		CrateToThrow->SetHeldByPlayerForFreshness(false);
	}
	else if (IsValid(TrashToThrow))
	{
		TrashToThrow->EnableWorldDropPhysics();
		TrashToThrow->ShowMeshAfterDrop();
	}
	else if (IsValid(ToolToThrow))
	{
		ToolToThrow->OnUnequipped();
		ToolToThrow->EnableWorldDropPhysics();
	}
	if (IsValid(QuickbarInventory)) QuickbarInventory->RemoveItem(ActorToThrow);

	const FVector ThrowDirection =
		(FirstPersonCamera->GetForwardVector() + FVector::UpVector * ThrowUpwardBias)
		.GetSafeNormal();
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	ActorToThrow->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (IsValid(PrimitiveComponent) && PrimitiveComponent->IsSimulatingPhysics())
		{
			PrimitiveComponent->AddImpulse(
				ThrowDirection * ThrowStrength,
				NAME_None,
				true);
		}
	}

	HeldActor = nullptr;
	DestroyPlacementPreview();
	if (IsValid(ToolToThrow)) ToolToThrow->OnReleasedToWorld(true);
}

bool ACSPlayerCharacter::TryPickUpBuildableFacility(AActor* FacilityActor)
{
	if (!IsBuildMode() ||
		IsValid(HeldActor) ||
		!IsValid(FacilityActor) ||
		HeldItemAnchor == nullptr ||
		!FacilityActor->Implements<UCSBuildableFacility>())
	{
		return false;
	}

	ResetBuildRotationInput();
	RestoreFacilityMagicWands(FacilityActor);
	const FRotator CurrentFacilityRotation = FacilityActor->GetActorRotation();
	const FRotator UprightFacilityRotation(
		0.0f,
		CurrentFacilityRotation.Yaw,
		0.0f);
	const bool bWasInstalled =
		ICSBuildableFacility::Execute_IsFacilityInstalled(FacilityActor);
	ICSBuildableFacility::Execute_PrepareForBuildPickup(FacilityActor);
	FacilityActor->SetActorRotation(
		UprightFacilityRotation,
		ETeleportType::TeleportPhysics);
	HeldBuildableFacilityRotation = UprightFacilityRotation;
	CacheHeldBuildableActorStates(FacilityActor);
	FacilityActor->SetActorEnableCollision(false);
	if (!FacilityActor->AttachToComponent(
		HeldItemAnchor,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale))
	{
		FacilityActor->SetActorEnableCollision(true);
		if (bWasInstalled)
		{
			ICSBuildableFacility::Execute_FinalizeBuildPlacement(FacilityActor);
		}
		else
		{
			ICSBuildableFacility::Execute_InitializeAsUninstalledFacility(FacilityActor);
		}
		RestoreHeldBuildableActorStates();
		HeldBuildableFacilityRotation = FRotator::ZeroRotator;
		return false;
	}

	HeldActor = FacilityActor;
	DestroyPlacementPreview();
	ConfigureBuildPlacementPreview(FacilityActor);
	HideHeldBuildableActors();
	UpdateBuildPlacementPreview();
	return true;
}

bool ACSPlayerCharacter::TryCalculateBuildPlacementLocation(
	AActor* FacilityActor,
	FVector& OutPlacementLocation,
	bool& OutIsSurfaceValid) const
{
	OutPlacementLocation = FVector::ZeroVector;
	OutIsSurfaceValid = false;
	if (!IsValid(FacilityActor) || FirstPersonCamera == nullptr || GetWorld() == nullptr)
	{
		return false;
	}

	const FVector AimTraceStart = FirstPersonCamera->GetComponentLocation();
	const FVector AimTraceEnd =
		AimTraceStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;

	FCollisionQueryParams BaseQueryParams;
	BaseQueryParams.AddIgnoredActor(this);
	BaseQueryParams.AddIgnoredActor(FacilityActor);
	TArray<AActor*> AttachedActors;
	FacilityActor->GetAttachedActors(AttachedActors, true, true);
	BaseQueryParams.AddIgnoredActors(AttachedActors);

	FHitResult AimHitResult;
	const bool bFoundAimTarget = GetWorld()->LineTraceSingleByChannel(
		AimHitResult,
		AimTraceStart,
		AimTraceEnd,
		ECC_Visibility,
		BaseQueryParams);
	const FVector AimPoint = bFoundAimTarget
		? AimHitResult.ImpactPoint
		: AimTraceEnd;

	const FVector FloorTraceEnd =
		AimPoint - FVector::UpVector * DropTraceDepth;
	FVector FloorTraceStart = AimPoint + FVector::UpVector * 50.0f;
	FCollisionQueryParams FloorQueryParams = BaseQueryParams;
	FHitResult FloorHitResult;
	bool bFoundValidFloor = false;
	constexpr float MinimumFloorNormalZ = 0.7f;
	constexpr int32 MaximumFloorTraceAttempts = 32;
	for (int32 TraceAttempt = 0;
		TraceAttempt < MaximumFloorTraceAttempts && FloorTraceStart.Z > FloorTraceEnd.Z;
		++TraceAttempt)
	{
		FHitResult CandidateHit;
		if (!GetWorld()->LineTraceSingleByChannel(
			CandidateHit,
			FloorTraceStart,
			FloorTraceEnd,
			ECC_Visibility,
			FloorQueryParams))
		{
			break;
		}

		AActor* HitActor = CandidateHit.GetActor();
		if (IsValid(HitActor) && HitActor->Implements<UCSBuildableFacility>())
		{
			FloorQueryParams.AddIgnoredActor(HitActor);
			continue;
		}

		const bool bIsFloorSurface =
			!CandidateHit.ImpactNormal.ContainsNaN() &&
			CandidateHit.ImpactNormal.Z >= MinimumFloorNormalZ &&
			CandidateHit.ImpactPoint.Z <= AimPoint.Z + KINDA_SMALL_NUMBER;
		if (bIsFloorSurface)
		{
			FloorHitResult = CandidateHit;
			bFoundValidFloor = true;
			break;
		}

		FloorTraceStart =
			CandidateHit.ImpactPoint - FVector::UpVector;
	}

	if (!bFoundValidFloor)
	{
		return false;
	}
	OutIsSurfaceValid = true;

	OutPlacementLocation = FloorHitResult.ImpactPoint;
	return !OutPlacementLocation.ContainsNaN();
}

void ACSPlayerCharacter::TryPlaceHeldBuildableFacility()
{
	if (!IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>())
	{
		return;
	}

	UpdateBuildPlacementPreview();
	if (!bHasBuildPlacementPreview)
	{
		return;
	}
	// 다른 시설과 겹치면 매장 구역 안팎과 무관하게 차단합니다.
	if (bHasHeldBuildablePlacementOverlap)
	{
		return;
	}
	// 손님 통로를 막는 자리도 같은 이유로 차단합니다.
	if (bBlockPlacementOnAccessZoneRules && bHasHeldBuildableAccessZoneConflict)
	{
		return;
	}
	// 매장 밖에는 내려놓지도 못하게 합니다.
	// 시설은 매장 구역 안에서만 손에서 떠나므로, 밖에 방치된 시설이 생기지 않습니다.
	if (!bIsHeldBuildableInsidePlacementArea)
	{
		return;
	}
	// 몸통은 매장 안인데 접근 영역만 밖으로 나간 경우입니다.
	// 손님이 못 오는 시설이 매장 한복판에 남으므로 마찬가지로 차단합니다.
	if (bBlockPlacementOnAccessZoneRules && !bIsHeldBuildableAccessZoneInsideArea)
	{
		return;
	}
	// 위 분기가 놓친 불가 조건이 나중에 추가되어도 여기서 함께 막힙니다.
	if (!bCanPlaceHeldBuildableFacility)
	{
		return;
	}

	AActor* FacilityToPlace = HeldActor;
	FacilityToPlace->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	FacilityToPlace->SetActorLocationAndRotation(
		CachedBuildPlacementLocation,
		CachedBuildPlacementRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	FacilityToPlace->SetActorEnableCollision(true);
	ICSBuildableFacility::Execute_FinalizeBuildPlacement(FacilityToPlace);
	RestoreHeldBuildableActorStates();
	ResetBuildRotationInput();
	HeldActor = nullptr;
	HeldBuildableFacilityRotation = FRotator::ZeroRotator;
	ClearBuildPlacementPreview();
	PlayPlayerGameplaySFX(this, ECSAudioEvent::FacilityPlace);

	// FinalizeBuildPlacement까지 끝난 실제 배치만 범용 성공 이벤트로 알립니다.
	OnBuildableFacilityPlaced.Broadcast(FacilityToPlace);
}

void ACSPlayerCharacter::TryUsePrimaryTool()
{
	if (IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSUsableTool>())
	{
		return;
	}

	// 현재 장착한 도구만 주 사용 입력을 처리하므로 청소 도구도 같은 흐름으로 확장할 수 있습니다.
	ICSUsableTool::Execute_UsePrimary(HeldActor, this);
}

void ACSPlayerCharacter::CreatePlacementPreview(ACSProductPickupActor* ProductActor)
{
	DestroyPlacementPreview();

	// 상품 프리뷰 클래스 또는 상품 데이터가 없으면 기존 배치 기능만 유지합니다.
	if (!IsValid(ProductActor) || PlacementPreviewActorClass == nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PlacementPreviewActor = GetWorld()->SpawnActor<ACSProductPlacementPreviewActor>(
		PlacementPreviewActorClass,
		FTransform::Identity,
		SpawnParameters);
	if (!IsValid(PlacementPreviewActor))
	{
		return;
	}

	// 생성 직후에는 숨겨 두고, 유효한 진열 슬롯을 조준할 때만 표시합니다.
	PlacementPreviewActor->SetActorHiddenInGame(true);
	if (!PlacementPreviewActor->InitializePreviewFromProduct(ProductActor))
	{
		DestroyPlacementPreview();
	}
}

void ACSPlayerCharacter::CreatePlacementPreviewFromCrate(
	const ACSDeliveryCrateActor* DeliveryCrate)
{
	DestroyPlacementPreview();

	if (!IsValid(DeliveryCrate) ||
		PlacementPreviewActorClass == nullptr ||
		DeliveryCrate->GetRemainingQuantity() < 1)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PlacementPreviewActor = GetWorld()->SpawnActor<ACSProductPlacementPreviewActor>(
		PlacementPreviewActorClass,
		FTransform::Identity,
		SpawnParameters);
	if (!IsValid(PlacementPreviewActor))
	{
		return;
	}

	PlacementPreviewActor->SetActorHiddenInGame(true);
	if (!PlacementPreviewActor->InitializePreviewFromProductClass(
		DeliveryCrate->GetProductRow(),
		DeliveryCrate->GetProductActorClass()))
	{
		DestroyPlacementPreview();
	}
}

bool ACSPlayerCharacter::TryPlaceProductFromCrate(
	ACSDeliveryCrateActor* DeliveryCrate,
	AActor* ShelfActor,
	FVector PlacementHint)
{
	if (!IsValid(DeliveryCrate) ||
		DeliveryCrate != HeldActor ||
		DeliveryCrate->GetRemainingQuantity() < 1 ||
		!IsValid(ShelfActor) ||
		!ShelfActor->Implements<UCSShelfPlaceable>())
	{
		return false;
	}

	ACSProductPickupActor* ProductActor = DeliveryCrate->SpawnProductActor();
	if (!IsValid(ProductActor))
	{
		return false;
	}

	if (!ICSShelfPlaceable::Execute_TryPlaceItem(
		ShelfActor,
		ProductActor,
		PlacementHint))
	{
		ProductActor->Destroy();
		return false;
	}

	const bool bWillDepleteCrate = DeliveryCrate->GetRemainingQuantity() == 1;
	if (!DeliveryCrate->ConsumeOne())
	{
		ICSShelfPlaceable::Execute_RemovePlacedItem(ShelfActor, ProductActor);
		ProductActor->Destroy();
		return false;
	}
	PlayPlayerGameplaySFX(this, ECSAudioEvent::ProductStock);

	if (bWillDepleteCrate || !IsValid(DeliveryCrate))
	{
		RemoveHeldItemFromQuickbar();
		HeldActor = nullptr;
		DestroyPlacementPreview();
	}
	else
	{
		// 상자에서 한 개를 진열하면 빈 슬롯 수가 변하므로 이 시점에만 다시 판정합니다.
		RefreshShelfPlacementHighlights();
	}

	return true;
}

void ACSPlayerCharacter::DestroyPlacementPreview()
{
	ClearCartPlacementPreview();
	ClearStorageCratePlacementPreview();
	ClearShelfPlacementHighlights();

	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController()))
	{
		// 상품을 내려놓거나 프리뷰를 교체할 때 배치 관련 안내도 함께 제거합니다.
		StorePlayerController->ClearInteractionContext();
	}

	if (IsValid(PlacementPreviewActor))
	{
		PlacementPreviewActor->Destroy();
	}

	PlacementPreviewActor = nullptr;
}

bool ACSPlayerCharacter::TryCalculateAccessZoneDisplayTransform(
	const FVector& ZoneCenter,
	const FVector& ZoneScaledExtent,
	float ZoneYaw,
	FVector& OutLocation,
	FRotator& OutRotation,
	FVector& OutScale) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	OutScale = FVector::OneVector;
	if (!IsValid(AccessZoneDisplayMesh) ||
		ZoneCenter.ContainsNaN() ||
		ZoneScaledExtent.ContainsNaN() ||
		!FMath::IsFinite(ZoneYaw))
	{
		return false;
	}

	// 두께가 없는 영역은 그릴 사각형 자체가 없으므로 호출부에서 건너뜀
	if (ZoneScaledExtent.X <= UE_KINDA_SMALL_NUMBER ||
		ZoneScaledExtent.Y <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	// 에셋의 바운드를 읽습니다.
	// 크기가 다른 에셋이 들어와도 유연하게 원래 판정영역에 대응하게 변경
	const FBox MeshBounds = AccessZoneDisplayMesh->GetBoundingBox();
	const FVector MeshSize = MeshBounds.GetSize();
	if (!MeshBounds.IsValid ||
		MeshSize.ContainsNaN() ||
		MeshSize.X <= UE_KINDA_SMALL_NUMBER ||
		MeshSize.Y <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// 영역 박스의 전체 폭을 Mesh의 전체 폭(MeshSize)으로 나눈 비율이 스케일
	// Z는 바닥에 눕는 표시물이라 늘리지 않고 1로 고정
	OutScale = FVector(
		ZoneScaledExtent.X * 2.0f / MeshSize.X,
		ZoneScaledExtent.Y * 2.0f / MeshSize.Y,
		1.0f);
	OutRotation = FRotator(0.0f, ZoneYaw, 0.0f);

	// Z-fighting 방지
	const FVector ZoneFloorCenter(
		ZoneCenter.X,
		ZoneCenter.Y,
		ZoneCenter.Z - ZoneScaledExtent.Z + AccessZoneDisplayLift);

	// 역보정 로직
	// 이때동안 비율과 위치를 통해 판정영역과 똑같이 대응되게 할당함
	const FVector MeshCenter = MeshBounds.GetCenter();
	const FVector MeshPivotOffset(
		MeshCenter.X * OutScale.X,
		MeshCenter.Y * OutScale.Y,
		MeshBounds.Min.Z * OutScale.Z);
	OutLocation = ZoneFloorCenter - OutRotation.RotateVector(MeshPivotOffset);

	return !OutLocation.ContainsNaN() && !OutScale.ContainsNaN();
}

UStaticMeshComponent* ACSPlayerCharacter::AcquireAccessZoneDisplay(int32 DisplayIndex)
{
	if (DisplayIndex < 0 || !IsValid(AccessZoneDisplayMesh))
	{
		return nullptr;
	}
	// 풀에 이미 만들어둔 그래픽이 있다면 새로 만들지 않고 그대로 반환
	if (AccessZoneDisplayMeshes.IsValidIndex(DisplayIndex))
	{
		return AccessZoneDisplayMeshes[DisplayIndex];
	}
	// 풀은 앞에서부터 순서대로만 채워지므로 한 칸씩만 늘어납니다.
	if (DisplayIndex != AccessZoneDisplayMeshes.Num())
	{
		return nullptr;
	}
	// 풀이 꽉 찼거나 처음 그리는 거라면, 새로운 Mesh 컴포넌트를 생성
	UStaticMeshComponent* Display = NewObject<UStaticMeshComponent>(
		this,
		NAME_None,
		RF_Transient);
	if (!IsValid(Display))
	{
		return nullptr;
	}

	// 필요없는 설정 제거
	Display->SetupAttachment(GetRootComponent());
	AddInstanceComponent(Display);
	Display->RegisterComponent();
	// 제자리에 있는 시설의 영역을 그리므로 플레이어를 따라다니면 안 됩니다.
	Display->SetAbsolute(true, true, true);
	// 표시물이므로 조준 트레이스와 손님 이동에 절대 관여하지 않습니다.
	Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Display->SetGenerateOverlapEvents(false);
	Display->SetCanEverAffectNavigation(false);
	Display->SetCastShadow(false);
	Display->SetStaticMesh(AccessZoneDisplayMesh);
	Display->SetHiddenInGame(false);
	Display->SetVisibility(false);

	// 충돌 시 실시간으로  색상을 바꾸기 위해, 동적 머티리얼 인스턴스를 생성해서 같이 저장
	UMaterialInstanceDynamic* DisplayMID = nullptr;
	if (IsValid(AccessZoneDisplayMaterial))
	{
		DisplayMID = Display->CreateAndSetMaterialInstanceDynamicFromMaterial(
			0,
			AccessZoneDisplayMaterial);
	}
	AccessZoneDisplayMeshes.Add(Display);
	AccessZoneDisplayMIDs.Add(DisplayMID);
	return Display;
}

// 맵 바닥에 그려져 있던 모든 주변 진열대들의 영역 평면 그래픽을 화면에서 숨김
// 파괴하는 것이 아니라 가시성만 꺼두어, 나중에 다시 건축 모드를 켰을 때 렉 없이 재사용할 수 있게 함
void ACSPlayerCharacter::ClearInstalledAccessZoneDisplays()
{
	for (UStaticMeshComponent* Display : AccessZoneDisplayMeshes)
	{
		if (IsValid(Display))
		{
			Display->SetVisibility(false);
		}
	}
}

void ACSPlayerCharacter::RefreshInstalledAccessZoneDisplays()
{
	// 시설을 들고 있을 때만 표시
	if (!IsBuildMode() ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>())
	{
		ClearInstalledAccessZoneDisplays();
		return;
	}

	// 메시가 지정되지 않았으면 무시
	if (!IsValid(AccessZoneDisplayMesh))
	{
		ClearInstalledAccessZoneDisplays();
		return;
	}

	int32 UsedCount = 0;
	TArray<UBoxComponent*> ZoneBoxes;
	// 저장해둔 맵 전체의 설치된 시설들을 순회
	for (const TWeakObjectPtr<AActor>& OtherWeak : CachedAccessZoneFacilities)
	{
		AActor* Other = OtherWeak.Get();
		if (!IsValid(Other) ||
			Other == HeldActor ||
			(!Other->IsA<ACSEmployeeWaitingArea>() && !ICSBuildableFacility::Execute_IsFacilityInstalled(Other)))
		{
			continue;
		}
		// 시각화 거리를 충돌 판정 거리보다 짧게 둡니다
		// 이유: 다 보이면 시각적으로 난잡할 수도 있음
		if (FVector::DistSquared2D(Other->GetActorLocation(), GetActorLocation()) >
			FMath::Square(AccessZoneVisualRange))
		{
			continue;
		}

		CollectBuildPlacementBoxesByTag(Other, BuildPlacementAccessZoneTag, ZoneBoxes);
		for (UBoxComponent* ZoneBox : ZoneBoxes)
		{
			const FVector ZoneExtent = ZoneBox->GetScaledBoxExtent();
			const FVector ZoneCenter = ZoneBox->GetComponentLocation();
			// 배치,크기 보정은 손에 든 프리뷰와 같은 함수를 써서 두 표시가 어긋나지 않게 함
			FVector DisplayLocation;
			FRotator DisplayRotation;
			FVector DisplayScale;
			if (!TryCalculateAccessZoneDisplayTransform(
					ZoneCenter,
					ZoneExtent,
					ZoneBox->GetComponentRotation().Yaw,
					DisplayLocation,
					DisplayRotation,
					DisplayScale))
			{
				continue;
			}

			// 지금 내 몸통이 침범 중인 영역 색 변경
			// 전부 같은 색이면 진열대가 몰려 있을 때 무엇 때문에 막혔는지 알 수 없음
			const bool bIsBlocked = CachedBlockedAccessZoneBoxes.Contains(
				TWeakObjectPtr<UBoxComponent>(ZoneBox));
			UStaticMeshComponent* Display = AcquireAccessZoneDisplay(UsedCount);
			if (!IsValid(Display))
			{
				continue;
			}

			Display->SetWorldLocation(DisplayLocation);
			Display->SetWorldRotation(DisplayRotation);
			Display->SetWorldScale3D(DisplayScale);
			// 내가 침범한 곳이면 Blocked, 안전한 곳이면 Neutral 파라미터로 머티리얼 색상을 변경
			if (AccessZoneDisplayMIDs.IsValidIndex(UsedCount) &&
				IsValid(AccessZoneDisplayMIDs[UsedCount]))
			{
				AccessZoneDisplayMIDs[UsedCount]->SetVectorParameterValue(
					TEXT("ZoneColor"),
					bIsBlocked ? BlockedZoneColor : NeutralZoneColor);
			}
			Display->SetVisibility(true);
			++UsedCount;
		}
	}

	// Tick에서 다 그리고 남은 그래픽 풀은 화면에 남지 않도록 다시 숨겨줌
	for (int32 DisplayIndex = UsedCount;
		DisplayIndex < AccessZoneDisplayMeshes.Num();
		++DisplayIndex)
	{
		if (IsValid(AccessZoneDisplayMeshes[DisplayIndex]))
		{
			AccessZoneDisplayMeshes[DisplayIndex]->SetVisibility(false);
		}
	}
}

void ACSPlayerCharacter::RefreshShelfPlacementHighlights()
{
	ClearShelfPlacementHighlights();

	if (!IsValid(HeldActor) || !IsValid(PlacementPreviewActor) || GetWorld() == nullptr)
	{
		return;
	}

	// 배송 상자는 상자 자체가 아닌, 내부 상품 형태의 프리뷰로 보관 적합성을 판정합니다.
	AActor* PlacementItem = Cast<ACSDeliveryCrateActor>(HeldActor) != nullptr
		? static_cast<AActor*>(PlacementPreviewActor)
		: HeldActor.Get();
	if (!IsValid(PlacementItem))
	{
		return;
	}

	for (TActorIterator<ACSShelfActor> ShelfIterator(GetWorld()); ShelfIterator; ++ShelfIterator)
	{
		ACSShelfActor* ShelfActor = *ShelfIterator;
		if (!IsValid(ShelfActor) || !ShelfActor->IsFacilityInstalled_Implementation())
		{
			continue;
		}

		// 기본 안내는 진열대 전체의 빈칸 기준입니다.
		// 가득 찬 진열대도 건너뛰지 않고 빨간 외곽선으로 알려 줍니다.
		const ECSShelfPlacementPreviewState PreviewState =
			ICSShelfPlaceable::Execute_CanPlaceItem(ShelfActor, PlacementItem)
				? ShelfActor->GetPlacementHighlightState(PlacementItem)
				: ECSShelfPlacementPreviewState::Invalid;
		ShelfActor->SetPlacementHighlight(PreviewState);
	}
}

void ACSPlayerCharacter::UpdateAimedShelfPlacementHighlight(
	ACSShelfActor* ShelfActor,
	uint8 PreviewStateValue)
{
	if (!IsValid(ShelfActor))
	{
		RestoreGlobalShelfPlacementHighlights();
		return;
	}

	if (AimedShelfPlacementHighlight.Get() == ShelfActor &&
		AimedShelfPlacementPreviewState == PreviewStateValue)
	{
		return;
	}

	// 이전 조준 단은 전체 빈칸 기준으로 되돌리고 새 단만 단별 결과를 표시합니다.
	RefreshShelfPlacementHighlights();
	AimedShelfPlacementHighlight = ShelfActor;
	AimedShelfPlacementPreviewState = PreviewStateValue;
	ShelfActor->SetPlacementHighlight(
		static_cast<ECSShelfPlacementPreviewState>(PreviewStateValue));
}

void ACSPlayerCharacter::RestoreGlobalShelfPlacementHighlights()
{
	if (!AimedShelfPlacementHighlight.IsValid())
	{
		return;
	}

	RefreshShelfPlacementHighlights();
}

void ACSPlayerCharacter::ClearShelfPlacementHighlights()
{
	if (GetWorld() == nullptr)
	{
		return;
	}

	for (TActorIterator<ACSShelfActor> ShelfIterator(GetWorld()); ShelfIterator; ++ShelfIterator)
	{
		if (ACSShelfActor* ShelfActor = *ShelfIterator; IsValid(ShelfActor))
		{
			ShelfActor->ClearPlacementHighlight();
		}
	}

	AimedShelfPlacementHighlight = nullptr;
	AimedShelfPlacementPreviewState = static_cast<uint8>(ECSShelfPlacementPreviewState::Invalid);
}

void ACSPlayerCharacter::SetPlacementPreviewVisible(bool bIsVisible)
{
	if (IsValid(PlacementPreviewActor))
	{
		PlacementPreviewActor->SetActorHiddenInGame(!bIsVisible);
	}
}

bool ACSPlayerCharacter::TryGetProductRowFromActor(
	const AActor* TargetActor,
	FDataTableRowHandle& OutProductRow) const
{
	OutProductRow = FDataTableRowHandle();
	if (const ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(TargetActor))
	{
		OutProductRow = ProductActor->GetProductRow();
	}
	else if (const ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(TargetActor))
	{
		OutProductRow = DeliveryCrate->GetProductRow();
	}

	return OutProductRow.DataTable != nullptr && !OutProductRow.RowName.IsNone();
}

void ACSPlayerCharacter::ShowProductPrice(
	const FDataTableRowHandle& ProductRow,
	int32 RemainingQuantity) const
{
	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSPlayerCharacter::ShowProductPrice"));
	if (!IsValid(StorePlayerController) || ProductData == nullptr)
	{
		return;
	}

	const UCSStorePricingSubsystem* PricingSubsystem =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<UCSStorePricingSubsystem>()
			: nullptr;
	const int32 CurrentSalePrice = IsValid(PricingSubsystem)
		? PricingSubsystem->GetSalePrice(ProductRow)
		: ProductData->RecommendedSalePrice;

	if (RemainingQuantity != INDEX_NONE)
	{
		// 배송 상자는 상품 가격과 함께 상자에 남은 수량을 표시합니다.
		StorePlayerController->SetProductPrice(FText::Format(
			NSLOCTEXT("Interaction", "CrateProductPrice", "판매가: {0} 골드 / 남은 수량: {1}개"),
			FText::AsNumber(CurrentSalePrice),
			FText::AsNumber(FMath::Max(0, RemainingQuantity))));
		return;
	}

	StorePlayerController->SetProductPrice(FText::Format(
		NSLOCTEXT("Interaction", "ProductPrice", "판매가: {0} 골드"),
		FText::AsNumber(CurrentSalePrice)));
}

void ACSPlayerCharacter::ShowProductShelfLife(
	const FCSProductRuntimeState& RuntimeState) const
{
	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(StorePlayerController) || !IsValid(StoreGameState))
	{
		return;
	}

	if (!RuntimeState.bExpirationInitialized ||
		RuntimeState.ExpirationBusinessDay <= 0)
	{
		return;
	}

	const int32 RemainingDays =
		RuntimeState.ExpirationBusinessDay - StoreGameState->CurrentBusinessDay;
	if (RemainingDays <= 0)
	{
		StorePlayerController->SetProductShelfLife(
			NSLOCTEXT("Interaction", "ProductShelfLifeExpired", "유통기한: 만료"),
			FLinearColor(1.0f, 0.25f, 0.25f, 1.0f));
		return;
	}

	const FLinearColor TextColor = RemainingDays == 1
		? FLinearColor(1.0f, 0.8f, 0.1f, 1.0f)
		: FLinearColor::White;
	StorePlayerController->SetProductShelfLife(
		FText::Format(
			NSLOCTEXT("Interaction", "ProductShelfLifeRemaining", "유통기한: {0}일 남음"),
			FText::AsNumber(RemainingDays)),
		TextColor);
}

void ACSPlayerCharacter::ShowProductFreshness(
	const FDataTableRowHandle& ProductRow,
	int32 CurrentFreshness) const
{
	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSPlayerCharacter::ShowProductFreshness"));
	if (!IsValid(StorePlayerController) || ProductData == nullptr)
	{
		return;
	}

	if (ProductData->ShelfType != ECSProductShelfType::ChilledShelf &&
		ProductData->ShelfType != ECSProductShelfType::FreezerShelf)
	{
		return;
	}

	const int32 DisplayFreshness = FMath::Clamp(CurrentFreshness, 0, 100);
	const FLinearColor TextColor = DisplayFreshness == 0
		? FLinearColor(1.0f, 0.25f, 0.25f, 1.0f)
		: DisplayFreshness <= 50
			? FLinearColor(1.0f, 0.8f, 0.1f, 1.0f)
			: FLinearColor::White;
	StorePlayerController->SetProductFreshness(
		FText::Format(
			NSLOCTEXT("Interaction", "ProductFreshness", "신선도: {0}%"),
			FText::AsNumber(DisplayFreshness)),
		TextColor);
}

void ACSPlayerCharacter::ShowStorageWarning(const FDataTableRowHandle& ProductRow) const
{
	ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController());
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSPlayerCharacter::ShowStorageWarning"));
	if (!IsValid(StorePlayerController) || ProductData == nullptr)
	{
		return;
	}

	if (ProductData->ShelfType == ECSProductShelfType::ChilledShelf)
	{
		StorePlayerController->SetStorageWarning(
			NSLOCTEXT("Interaction", "ChilledStorageWarning", "냉장 보관이 필요한 상품입니다."));
	}
	else if (ProductData->ShelfType == ECSProductShelfType::FreezerShelf)
	{
		StorePlayerController->SetStorageWarning(
			NSLOCTEXT("Interaction", "FrozenStorageWarning", "냉동 보관이 필요한 상품입니다."));
	}
}

void ACSPlayerCharacter::ShowEmployeeReservationWarning(const FText& Message) const
{
	if (ACSStorePlayerController* StorePlayerController = Cast<ACSStorePlayerController>(GetController()))
	{
		StorePlayerController->SetStorageWarning(Message);
	}
}

void ACSPlayerCharacter::PrepareInventoryForPlayerPickup(AActor* Inventory)
{
	if (!IsValid(Inventory) || (!Inventory->IsA<ACSProductPickupActor>() && !Inventory->IsA<ACSDeliveryCrateActor>())) return;
	if (UCSStoreEmployeeSubsystem* Employees = GetWorld()->GetSubsystem<UCSStoreEmployeeSubsystem>())
		Employees->CancelTaskForPlayerPickup(Inventory);
}

TArray<FCSInteractionPrompt> ACSPlayerCharacter::GetShelfInteractionPrompts(const ACSShelfActor* Shelf, FVector AimPoint, const ACSProductPickupActor* DirectProduct) const
{
	TArray<FCSInteractionPrompt> Prompts;
	const FText Manage = NSLOCTEXT("Packing", "ManageShelf", "[F] 진열 구성 관리");
	if (!IsValid(Shelf)) return Prompts;
	const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor);
	const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(HeldActor);
	// 물건을 든 동안에는 실제로 열 수 없는 구성 관리 안내를 표시하지 않습니다.
	if (IsValid(HeldActor) && !HeldProduct && !Crate) return Prompts;
	const FDataTableRowHandle Row = HeldProduct ? HeldProduct->GetProductRow() : Crate ? Crate->GetProductRow() : FDataTableRowHandle();
	const ACSProductPickupActor* Candidate = DirectProduct;
	if (!Candidate || Candidate->GetAttachParentActor() != Shelf || !ACSDeliveryCrateActor::CanPackProduct(Candidate, this) ||
		(!Row.IsNull() && (Row.DataTable != Candidate->GetProductRow().DataTable || Row.RowName != Candidate->GetProductRow().RowName)))
		Candidate = Shelf->FindRetrievableProduct(AimPoint, Row, this);
	// 빈 단에는 회수 안내를 생략하되, 들고 계신 상품의 배치 안내는 유지합니다.
	if (Shelf->GetProductCountOnAimedLevel(AimPoint) > 0)
		Prompts.Add({GetProductCollectionPrompt(Candidate), !CanCollectProductForPrompt(Candidate)});
	if (IsValid(HeldActor))
	{
		ACSProductPickupActor* PlacementProduct = HeldProduct ? const_cast<ACSProductPickupActor*>(HeldProduct) : PlacementPreviewActor.Get();
		if (IsValid(PlacementProduct))
		{
			FTransform PreviewTransform;
			const ECSShelfPlacementPreviewState State = Shelf->GetPlacementPreviewState_Implementation(PlacementProduct, AimPoint, PreviewTransform);
			const bool bInvalid = State == ECSShelfPlacementPreviewState::Invalid;
			Prompts.Add({bInvalid ? Shelf->GetPlayerPlacementFailureText(PlacementProduct, AimPoint)
				: NSLOCTEXT("Interaction", "PlacePrompt", "[Q] 진열하기"), bInvalid});
		}
		else if (Crate)
		{
			Prompts.Add({FText::FromString(Crate->GetRemainingQuantity() == 0
				? TEXT("상자에 진열할 상품이 없습니다.") : TEXT("진열 미리보기를 표시할 수 없습니다.")), true});
		}
	}
	if (!IsValid(HeldActor)) Prompts.Add({Manage, false});
	return Prompts;
}

void ACSPlayerCharacter::UpdateHeldCollectionHUD()
{
	ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(GetController());
	if (!StoreController || StoreController->IsGameplayUIInputLocked() || !FirstPersonCamera) return;
	FHitResult Hit;
	FCollisionQueryParams Query;
	Query.AddIgnoredActor(this);
	Query.AddIgnoredActor(HeldActor);
	const FVector Start = FirstPersonCamera->GetComponentLocation();
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + FirstPersonCamera->GetForwardVector() * InteractionDistance, ECC_Visibility, Query)) return;
	AActor* Target = Hit.GetActor();
	if (const ACSStoreEmployeeCharacter* Employee = Cast<ACSStoreEmployeeCharacter>(Target))
		if (AActor* Inventory = Employee->GetCarriedInventoryForPlayer()) Target = Inventory;
	ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Target);
	ACSShelfActor* Shelf = Cast<ACSShelfActor>(Target);
	if (!Shelf && Product) Shelf = Cast<ACSShelfActor>(Product->GetAttachParentActor());
	if (Shelf)
	{
		StoreController->SetInteractionPrompts(GetShelfInteractionPrompts(Shelf, Hit.ImpactPoint, Product));
	}
	else if (Product) StoreController->SetInteractionPrompts({{GetProductCollectionPrompt(Product), !CanCollectProductForPrompt(Product)}});
}

bool ACSPlayerCharacter::CanCollectProductForPrompt(const ACSProductPickupActor* Product) const
{
	if (!ACSDeliveryCrateActor::CanPackProduct(Product, this)) return false;
	if (!IsValid(HeldActor)) return true;
	const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(HeldActor);
	const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor);
	if (Crate && (!Crate->IsPackingCrate() || Crate->GetRemainingQuantity() >= ACSDeliveryCrateActor::PackingCapacity)) return false;
	if (!Crate && (!HeldProduct || !PackingCrateClass)) return false;
	const FDataTableRowHandle Row = Crate ? Crate->GetProductRow() : HeldProduct->GetProductRow();
	return Row.DataTable == Product->GetProductRow().DataTable && Row.RowName == Product->GetProductRow().RowName;
}

FText ACSPlayerCharacter::GetProductCollectionPrompt(const ACSProductPickupActor* Product) const
{
	const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(HeldActor);
	if (IsValid(Crate) && (!Crate->IsPackingCrate() || Crate->GetRemainingQuantity() >= ACSDeliveryCrateActor::PackingCapacity))
		return FText::FromString(Crate->IsPackingCrate() ? TEXT("상자가 가득 찼습니다 · 6/6") : TEXT("배송 상자에는 상품을 추가할 수 없습니다."));
	if (!IsValid(Product)) return FText::FromString(TEXT("회수 불가: 이 단에 같은 종류의 상품이 없습니다."));
	if (!ACSDeliveryCrateActor::CanPackProduct(Product, this)) return FText::FromString(TEXT("회수할 수 없는 상품입니다."));
	if (!IsValid(HeldActor)) return FText::Format(NSLOCTEXT("Packing", "PickOne", "[E] {0} 회수"), Product->GetProductDisplayName());
	const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor);
	const FDataTableRowHandle Row = Crate ? Crate->GetProductRow() : HeldProduct ? HeldProduct->GetProductRow() : FDataTableRowHandle();
	if (Row.DataTable != Product->GetProductRow().DataTable || Row.RowName != Product->GetProductRow().RowName)
		return FText::FromString(TEXT("회수 불가: 같은 종류의 상품만 담을 수 있습니다."));
	if (HeldProduct && !PackingCrateClass) return FText::FromString(TEXT("회수 불가: 포장 상자가 설정되지 않았습니다."));
	// 새 상자를 처음 만들 때는 아직 손에 든 상품까지 합산한 수량을 표시하지 않습니다.
	if (HeldProduct) return FText::Format(NSLOCTEXT("Packing", "CreateBox", "[E] {0} 회수하여 상자 만들기"), Product->GetProductDisplayName());
	return FText::Format(NSLOCTEXT("Packing", "Collect", "[E] {0} 회수 · 상자 {1}/6"),
		Product->GetProductDisplayName(), FText::AsNumber(Crate ? Crate->GetRemainingQuantity() : 1));
}

void ACSPlayerCharacter::ShowProductCollectionFailure(const ACSProductPickupActor* Product) const
{
	// 회수 실패 입력도 조준 안내와 동일한 빨간 위험 스타일로 표시합니다.
	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(GetController()))
		StoreController->SetInteractionPrompt(GetProductCollectionPrompt(Product), FLinearColor::Red);
}

bool ACSPlayerCharacter::TryCollectProduct(ACSProductPickupActor* Product)
{
	if (IsBuildMode() || IsValid(PushedCart) || !IsValid(HeldItemAnchor) || Product == HeldActor ||
		!ACSDeliveryCrateActor::CanPackProduct(Product, this)) return false;
	if (!IsValid(HeldActor)) return TryPickUp(Product);
	if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(HeldActor))
	{
		// 담을 수 없는 입력으로 직원 업무가 취소되지 않도록 용량과 품목을 먼저 확인합니다.
		if (Crate->IsPackingCrate() && Crate->GetRemainingQuantity() < ACSDeliveryCrateActor::PackingCapacity &&
			Crate->GetProductRow().DataTable == Product->GetProductRow().DataTable && Crate->GetProductRow().RowName == Product->GetProductRow().RowName)
			PrepareInventoryForPlayerPickup(Product);
		if (Crate->TryAddPackedProduct(Product, this)) return true;
		ShowProductCollectionFailure(Product);
		return false;
	}
	ACSProductPickupActor* First = Cast<ACSProductPickupActor>(HeldActor);
	if (!IsValid(First) || !PackingCrateClass || First->GetProductRow().DataTable != Product->GetProductRow().DataTable ||
		First->GetProductRow().RowName != Product->GetProductRow().RowName)
	{
		ShowProductCollectionFailure(Product);
		return false;
	}
	// 상품의 개별 크기 배율이 새 상자에 복사되지 않도록 위치만 전달합니다.
	const FTransform Transform(FRotator::ZeroRotator, First->GetActorLocation());
	ACSDeliveryCrateActor* Crate = GetWorld()->SpawnActorDeferred<ACSDeliveryCrateActor>(PackingCrateClass, Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Crate)) return false;
	if (!Crate->InitializePackedProducts({First, Product})) { Crate->Destroy(); return false; }
	Crate->FinishSpawning(Transform);
	if (!IsValid(Crate)) return false;
	// 집기 실패 시 기존 상품을 그대로 유지하여 회수 중 재고가 사라지지 않게 합니다.
	HeldActor = nullptr;
	if (!TryPickUp(Crate)) { HeldActor = First; Crate->Destroy(); return false; }
	PrepareInventoryForPlayerPickup(Product);
	ACSDeliveryCrateActor::RemovePackedSource(First);
	ACSDeliveryCrateActor::RemovePackedSource(Product);
	return true;
}

void ACSPlayerCharacter::TryInteract()
{
	// 수레를 잡은 상태에서는 E로 물건을 집거나 다른 상호작용을 실행하지 않습니다.
	if (IsValid(PushedCart))
	{
		return;
	}

    if (FirstPersonCamera == nullptr)
    {
        return;
    }

    const FVector TraceStart = FirstPersonCamera->GetComponentLocation();
    const FVector TraceEnd = TraceStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;

    FHitResult HitResult;

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);

    const bool bHit = GetWorld()->LineTraceSingleByChannel(
        HitResult,
        TraceStart,
        TraceEnd,
        ECC_Visibility,
        QueryParams);

    DrawDebugLine(
        GetWorld(),
        TraceStart,
        TraceEnd,
        bHit ? FColor::Green : FColor::Red,
        false,
        1.0f,
        0,
        1.0f);

    if (!bHit)
    {
        return;
    }

    AActor* HitActor = HitResult.GetActor();
	if (const ACSStoreEmployeeCharacter* Employee = Cast<ACSStoreEmployeeCharacter>(HitActor))
		if (AActor* Inventory = Employee->GetCarriedInventoryForPlayer()) HitActor = Inventory;
	if (IsBuildMode())
	{
		if (IsValid(HitActor) && HitActor->Implements<UCSBuildableFacility>())
		{
			TryPickUpBuildableFacility(HitActor);
		}
		return;
	}

	if (ACSFreshnessSafeZoneActor* StoragePoint = Cast<ACSFreshnessSafeZoneActor>(HitActor))
	{
		const UPrimitiveComponent* HitComponent = HitResult.GetComponent();
		int32 UnusedSlotIndex = INDEX_NONE;
		if (StoragePoint->TryGetCrateStorageSlotIndex(HitComponent, UnusedSlotIndex))
		{
			if (!IsValid(HeldActor))
			{
				TryTakeStoredCrate(StoragePoint, HitComponent);
			}
			return;
		}

		if (StoragePoint->IsLooseProductDepositComponent(HitComponent))
		{
			return;
		}
	}

    if (ACSEndingDisplayActor* Display = Cast<ACSEndingDisplayActor>(HitActor))
    {
		// 액터 전체가 아닌 실제 조준한 슬롯으로 배치·회수를 처리합니다.
		Display->InteractWithSlot(this, HitResult.GetComponent());
		return;
    }

	ACSProductPickupActor* HitProduct = Cast<ACSProductPickupActor>(HitActor);
	ACSShelfActor* Shelf = Cast<ACSShelfActor>(HitActor);
	if (!Shelf && HitProduct) Shelf = Cast<ACSShelfActor>(HitProduct->GetAttachParentActor());
	if (Shelf)
	{
		if (Shelf->GetProductCountOnAimedLevel(HitResult.ImpactPoint) == 0) return;
		FDataTableRowHandle RequiredRow;
		if (const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor)) RequiredRow = HeldProduct->GetProductRow();
		else if (const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(HeldActor)) RequiredRow = Crate->GetProductRow();
		else if (IsValid(HeldActor)) return;
		const bool bDirectMatch = HitProduct && ACSDeliveryCrateActor::CanPackProduct(HitProduct, this) &&
			(RequiredRow.IsNull() || (RequiredRow.DataTable == HitProduct->GetProductRow().DataTable && RequiredRow.RowName == HitProduct->GetProductRow().RowName));
		ACSProductPickupActor* Product = bDirectMatch ? HitProduct : Shelf->FindRetrievableProduct(HitResult.ImpactPoint, RequiredRow, this);
		if (Product) TryCollectProduct(Product);
		else ShowProductCollectionFailure(nullptr);
		return;
	}
	if (HitProduct) { TryCollectProduct(HitProduct); return; }

    if (IsValid(HitActor) && HitActor->Implements<UCSInteractable>())
    {
        ICSInteractable::Execute_Interact(HitActor, this);
    }
}

void ACSPlayerCharacter::ClearCartPlacementPreview()
{
	if (IsValid(CartPlacementPreviewTarget))
	{
		CartPlacementPreviewTarget->HideCratePlacementPreview();
	}
	CartPlacementPreviewTarget = nullptr;
}

void ACSPlayerCharacter::ClearStorageCratePlacementPreview()
{
	if (IsValid(StorageCratePlacementPreviewTarget))
	{
		StorageCratePlacementPreviewTarget->HideCrateStoragePreview();
	}
	StorageCratePlacementPreviewTarget = nullptr;
}

void ACSPlayerCharacter::ToggleCartPush()
{
	if (IsBuildMode())
	{
		// 기존 F의 Started 입력당 한 번만, 프리뷰와 최종 배치가 공유하는 회전값을 갱신합니다.
		ApplyBuildRotationStep(45.0f);
		return;
	}

	if (IsValid(PushedCart))
	{
		PushedCart->StopPushing(this);
		PushedCart = nullptr;
		return;
	}

	if (IsValid(HeldActor) || FirstPersonCamera == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	const FVector TraceStart = FirstPersonCamera->GetComponentLocation();
	const FVector TraceEnd = TraceStart + FirstPersonCamera->GetForwardVector() * InteractionDistance;
	if (!GetWorld()->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams))
	{
		return;
	}

	if (ACSShelfActor* Shelf = Cast<ACSShelfActor>(HitResult.GetActor()); IsValid(Shelf))
	{
		// F는 수레 조작을 우선 유지하되, 수레가 아닌 진열대를 조준한 경우에는 구성 관리 UI를 엽니다.
		Shelf->OpenShelfConfiguration(this);
		return;
	}

	if (ACSCartActor* Cart = Cast<ACSCartActor>(HitResult.GetActor());
		IsValid(Cart) && Cart->BeginPushing(this))
	{
		PushedCart = Cart;
	}
}

void ACSPlayerCharacter::SelectQuickbarSlot1()
{
	TrySelectQuickbarSlot(0);
}

void ACSPlayerCharacter::SelectQuickbarSlot2()
{
	TrySelectQuickbarSlot(1);
}

void ACSPlayerCharacter::SelectQuickbarSlot3()
{
	TrySelectQuickbarSlot(2);
}

void ACSPlayerCharacter::SelectQuickbarSlot4()
{
	TrySelectQuickbarSlot(3);
}

void ACSPlayerCharacter::SelectQuickbarSlot5()
{
	TrySelectQuickbarSlot(4);
}

void ACSPlayerCharacter::SelectQuickbarSlot6()
{
	TrySelectQuickbarSlot(5);
}

void ACSPlayerCharacter::SelectNextQuickbarSlot()
{
	if (IsValid(QuickbarInventory) &&
		!IsBuildMode() &&
		!IsValid(PushedCart) &&
		(!IsValid(HeldActor) || QuickbarInventory->ContainsItem(HeldActor)))
	{
		QuickbarInventory->CycleSelection(1);
	}
}

void ACSPlayerCharacter::SelectPreviousQuickbarSlot()
{
	if (IsValid(QuickbarInventory) &&
		!IsBuildMode() &&
		!IsValid(PushedCart) &&
		(!IsValid(HeldActor) || QuickbarInventory->ContainsItem(HeldActor)))
	{
		QuickbarInventory->CycleSelection(-1);
	}
}

bool ACSPlayerCharacter::TrySelectQuickbarSlot(int32 SlotIndex)
{
	if (!IsValid(QuickbarInventory) || IsBuildMode() || IsValid(PushedCart))
	{
		return false;
	}

	if (IsValid(HeldActor) && !QuickbarInventory->ContainsItem(HeldActor))
	{
		if (ACSStorePlayerController* StorePlayerController =
			Cast<ACSStorePlayerController>(GetController()))
		{
			StorePlayerController->SetInteractionPrompt(
				NSLOCTEXT("Quickbar", "PutDownHeldItem", "손에 든 물건을 먼저 내려놓으세요."),
				FLinearColor(1.0f, 0.8f, 0.3f, 1.0f));
		}
		return false;
	}

	if (!QuickbarInventory->SelectSlot(SlotIndex))
	{
		return false;
	}

	// 이미 선택된 칸을 다시 눌러도 도구 장착 상태를 복구할 수 있게 합니다.
	EquipSelectedQuickbarTool();
	return true;
}

void ACSPlayerCharacter::GetMagicWandIKTargets(FTransform& HandTarget, FVector& ElbowTarget) const
{
	HandTarget = FTransform::Identity;
	ElbowTarget = FVector::ZeroVector;
	if (!IsValid(GetMesh()) || !IsValid(FirstPersonCamera) ||
		!IsValid(MagicWandHandTarget) || !IsValid(MagicWandElbowTarget))
	{
		return;
	}

	FTransform CameraTransform = FirstPersonCamera->GetComponentTransform();
	if (FirstPersonCamera->bUsePawnControlRotation)
	{
		// CameraManager가 카메라를 갱신하기 전에도 이번 프레임의 시선을 사용합니다.
		CameraTransform.SetRotation(GetViewRotation().Quaternion());
	}
	const FTransform& MeshTransform = GetMesh()->GetComponentTransform();
	HandTarget = (MagicWandHandTarget->GetRelativeTransform() * CameraTransform)
		.GetRelativeTransform(MeshTransform);
	ElbowTarget = MeshTransform.InverseTransformPosition(
		MagicWandElbowTarget->GetComponentLocation());
}

USceneComponent* ACSPlayerCharacter::GetHeldItemAttachTarget(const AActor* ItemActor) const
{
	if (!IsValid(ItemActor))
	{
		return nullptr;
	}
	if (!ItemActor->IsA<ACSMagicWandActor>())
	{
		return HeldItemAnchor;
	}

	const USkeletalMeshComponent* PlayerMesh = GetMesh();
	if (!IsValid(MagicWandAnchor) || !IsValid(PlayerMesh) ||
		PlayerMesh->GetBoneIndex(MagicWandAttachBoneName) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: Cannot equip magic wand: MagicWandAnchor or RightHand bone is missing."),
			*GetName());
		return nullptr;
	}
	return MagicWandAnchor;
}

void ACSPlayerCharacter::EquipSelectedQuickbarTool()
{
	if (!IsValid(QuickbarInventory) || !IsValid(QuickbarStorageAnchor) ||
		HeldItemAnchor == nullptr)
	{
		return;
	}

	AActor* SelectedItem = QuickbarInventory->GetItemAt(
		QuickbarInventory->GetSelectedSlotIndex());
	if (IsValid(HeldActor) && !QuickbarInventory->ContainsItem(HeldActor))
	{
		return;
	}
	if (HeldActor == SelectedItem)
	{
		return;
	}

	if (IsValid(HeldActor))
	{
		StoreHeldQuickbarItem();
	}

	if (!IsValid(SelectedItem))
	{
		return;
	}

	if (ACSUsableToolActor* Tool = Cast<ACSUsableToolActor>(SelectedItem)) Tool->PrepareForPickupPhysics();
	else if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(SelectedItem))
	{
		Crate->PrepareForPickupPhysics();
		Crate->SetHeldByPlayerForFreshness(true);
	}
	else if (ACSTrashActor* Trash = Cast<ACSTrashActor>(SelectedItem)) Trash->PrepareForPickupPhysics();
	USceneComponent* AttachTarget = GetHeldItemAttachTarget(SelectedItem);
	if (!IsValid(AttachTarget))
	{
		return;
	}
	SelectedItem->AttachToComponent(
		AttachTarget,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SelectedItem->SetActorHiddenInGame(false);
	SelectedItem->SetActorEnableCollision(false);
	if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(SelectedItem))
	{
		Crate->SetActorRelativeLocation(Crate->GetHeldRelativeLocation());
		CreatePlacementPreviewFromCrate(Crate);
	}
	else DestroyPlacementPreview();
	if (ACSUsableToolActor* Tool = Cast<ACSUsableToolActor>(SelectedItem)) Tool->OnEquipped(this);
	HeldActor = SelectedItem;
	RefreshShelfPlacementHighlights();
}

void ACSPlayerCharacter::StoreHeldQuickbarItem()
{
	if (!IsValid(HeldActor) || !IsValid(QuickbarInventory) ||
		!QuickbarInventory->ContainsItem(HeldActor) || !IsValid(QuickbarStorageAnchor)) return;
	if (Cast<ACSUsableToolActor>(HeldActor) != nullptr)
	{
		StoreEquippedQuickbarTool();
		return;
	}
	AActor* Item = HeldActor;
	HeldActor = nullptr;
	DestroyPlacementPreview();
	ClearShelfPlacementHighlights();
	Item->SetActorEnableCollision(false);
	Item->AttachToComponent(QuickbarStorageAnchor,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Item->SetActorHiddenInGame(true);
	// 상자 액터의 신선도 타이머는 유지하고 냉장·냉동 보호만 받지 않습니다.
	if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Item))
		Crate->SetHeldByPlayerForFreshness(true);
}

void ACSPlayerCharacter::RemoveHeldItemFromQuickbar()
{
	if (IsValid(QuickbarInventory) && HeldActor != nullptr)
		QuickbarInventory->RemoveItem(HeldActor);
}

bool ACSPlayerCharacter::ConsumeQuickbarTool(ACSUsableToolActor* Tool)
{
	if (!ReleaseQuickbarTool(Tool)) return false;
	Tool->Destroy();
	return true;
}

bool ACSPlayerCharacter::ReleaseQuickbarTool(ACSUsableToolActor* Tool)
{
	if (!IsValid(Tool) || !IsValid(QuickbarInventory) || !QuickbarInventory->ContainsTool(Tool))
	{
		return false;
	}
	if (HeldActor == Tool)
	{
		HeldActor = nullptr;
	}
	QuickbarInventory->RemoveTool(Tool);
	Tool->PrepareForPickupPhysics();
	Tool->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Tool->OnUnequipped();
	return true;
}

void ACSPlayerCharacter::StoreEquippedQuickbarTool()
{
	ACSUsableToolActor* EquippedTool = Cast<ACSUsableToolActor>(HeldActor);
	if (!IsValid(EquippedTool) || !IsValid(QuickbarInventory) ||
		!QuickbarInventory->ContainsTool(EquippedTool) || !IsValid(QuickbarStorageAnchor))
	{
		return;
	}

	HeldActor = nullptr;
	EquippedTool->StoreInQuickbar(QuickbarStorageAnchor);
}

void ACSPlayerCharacter::UpdateQuickbarHUD()
{
	if (!IsValid(QuickbarInventory))
	{
		return;
	}

	TArray<FText> ToolNames;
	TArray<UTexture2D*> ToolIcons;
	const int32 SlotCount = QuickbarInventory->GetSlotCount();
	ToolNames.Reserve(SlotCount);
	ToolIcons.Reserve(SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		AActor* Item = QuickbarInventory->GetItemAt(SlotIndex);
		if (const ACSUsableToolActor* Tool = Cast<ACSUsableToolActor>(Item))
		{
			ToolNames.Add(Tool->GetToolDisplayName());
			ToolIcons.Add(Tool->GetToolIcon());
		}
		else if (const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Item))
		{
			ToolNames.Add(Crate->GetProductDisplayName());
			ToolIcons.Add(Crate->GetProductIcon().LoadSynchronous());
		}
		else if (const ACSTrashActor* Trash = Cast<ACSTrashActor>(Item))
		{
			// 제조 실패물은 종류 이름으로 보여 준다
			ToolNames.Add(Trash->GetTrashDisplayName());
			ToolIcons.Add(nullptr);
		}
		else
		{
			ToolNames.Add(FText::GetEmpty());
			ToolIcons.Add(nullptr);
		}
	}

	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController()))
	{
		StorePlayerController->SetQuickbarHUDData(
			SlotCount,
			QuickbarInventory->GetSelectedSlotIndex(),
			ToolNames,
			ToolIcons);
	}
}

void ACSPlayerCharacter::HandleQuickbarChanged()
{
	UpdateQuickbarHUD();
}

void ACSPlayerCharacter::HandleQuickbarSelectionChanged(int32 SelectedSlotIndex)
{
	(void)SelectedSlotIndex;
	EquipSelectedQuickbarTool();
	UpdateQuickbarHUD();
}

void ACSPlayerCharacter::HandleStoreLevelChanged(int32 NewLevel)
{
	if (IsValid(QuickbarInventory))
		QuickbarInventory->SetSlotCount(FMath::Clamp(NewLevel + 1, 2, 6));
}
