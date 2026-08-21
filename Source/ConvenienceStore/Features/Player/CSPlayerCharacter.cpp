#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Cart/CSCartActor.h"
#include "Features/Checkout/CSCheckoutCounterActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"
#include "Features/Tools/CSUsableTool.h"
#include "Features/Tools/CSUsableToolActor.h"
#include "Features/Tools/CSMagicWandActor.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Data/CSProductTypes.h"
#include "Features/Trash/CSTrashActor.h"
#include "Features/Trash/CSTrashCanActor.h"

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
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Systems/Audio/CSAudioSubsystem.h"

namespace
{
const FName BuildPlacementCollisionTag(TEXT("BuildPlacementCollision"));
const FName BuildPlacementAreaTag(TEXT("BuildPlacementArea"));

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

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(
        TEXT("FirstPersonCamera"));

    // 카메라를 캡슐 컴포넌트에 연결합니다.
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(-10.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    HeldItemAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("HeldItemAnchor"));
    HeldItemAnchor->SetupAttachment(FirstPersonCamera);
    HeldItemAnchor->SetRelativeLocation(FVector(80.0f, 20.0f, -20.0f));

	CartPushAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("CartPushAnchor"));
	CartPushAnchor->SetupAttachment(GetCapsuleComponent());
	// 기본값은 편집기에서 보이는 기준점이며, 실제 손잡이 높이는 플레이어 BP에서 조절합니다.
	CartPushAnchor->SetRelativeLocation(FVector(120.0f, 0.0f, -72.0f));

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
	if (IsValid(PushedCart))
	{
		// 플레이어가 제거될 때 수레의 Pawn 충돌 응답도 원래대로 되돌립니다.
		PushedCart->StopPushing(this);
		PushedCart = nullptr;
	}
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
        DropAction, ETriggerEvent::Started, this, &ACSPlayerCharacter::DropHeldItem);

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

    AddControllerYawInput(LookAxisVector.X);
    AddControllerPitchInput(LookAxisVector.Y);
}

void ACSPlayerCharacter::StartSprint()
{
	// 웅크린 상태에서는 달리기를 시작하지 않도록 제한합니다.
	if (bIsCrouched)
	{
		return;
	}

    GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
}

void ACSPlayerCharacter::StopSprint()
{
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
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

	if (IsBuildMode())
	{
		UpdateBuildRotationInput(DeltaTime);
		UpdateBuildPlacementPreview();
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
		if (HeldActor->Implements<UCSUsableTool>())
		{
			// 마법봉은 원래 거치대를 조준한 경우에만 Q 안내를 표시합니다.
			UpdateHeldMagicWandDockHUD();
		}
		else
		{
			UpdatePlacementPreview();
		}
	}
	else
	{
		UpdateInteractionHUD();
	}
}

bool ACSPlayerCharacter::TryPickUp(AActor* PickupActor)
{
	if (IsBuildMode() ||
		!IsValid(PickupActor) ||
		PickupActor->Implements<UCSBuildableFacility>() ||
		IsValid(PushedCart) ||
		IsValid(HeldActor) ||
		HeldItemAnchor == nullptr)
    {
        return false;
    }

    // 진열대에서 다시 집은 상품은 기존 슬롯 점유를 먼저 해제합니다.
	if (AActor* PreviousParent = PickupActor->GetAttachParentActor();
		IsValid(PreviousParent) && PreviousParent->Implements<UCSShelfPlaceable>())
	{
		ICSShelfPlaceable::Execute_RemovePlacedItem(PreviousParent, PickupActor);
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

		// 쓰레기 메시가 화면을 너무 가려서 주웠을 때 메시 가림
		TrashActor->HideMeshForPickup();
	}

	PickupActor->SetActorEnableCollision(false);
	PickupActor->AttachToComponent(
		HeldItemAnchor,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (const ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(PickupActor))
	{
		PickupActor->SetActorRelativeLocation(
			DeliveryCrate->GetHeldRelativeLocation());
	}

	HeldActor = PickupActor;

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
		return;
	}

	AActor* TargetActor = HitResult.GetActor();
	if (!IsValid(TargetActor))
	{
		return;
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

	if (!TargetActor->Implements<UCSInteractable>())
	{
		return;
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
		FText::Format(
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
			StorePlayerController->SetInteractionPrompt(
				NSLOCTEXT("Interaction", "BuildPlaceFacilityPrompt", "[Q] 시설 배치"),
				FLinearColor::White);
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
}

void ACSPlayerCharacter::ClearBuildPlacementPreview()
{
	bHasBuildPlacementPreview = false;
	bCanPlaceHeldBuildableFacility = false;
	bIsHeldBuildableInsidePlacementArea = true;
	bHasHeldBuildablePlacementOverlap = false;
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

void ACSPlayerCharacter::CollectBuildPlacementCollisionBoxes(
	AActor* FacilityActor,
	TArray<UBoxComponent*>& OutCollisionBoxes) const
{
	OutCollisionBoxes.Reset();
	if (!IsValid(FacilityActor))
	{
		return;
	}

	TArray<UBoxComponent*> BoxComponents;
	FacilityActor->GetComponents<UBoxComponent>(BoxComponents);
	for (UBoxComponent* BoxComponent : BoxComponents)
	{
		if (IsValid(BoxComponent) &&
			BoxComponent->ComponentHasTag(BuildPlacementCollisionTag))
		{
			OutCollisionBoxes.Add(BoxComponent);
		}
	}
}

bool ACSPlayerCharacter::IsBuildPlacementInsideCachedAreas(
	const TArray<UBoxComponent*>& CollisionBoxes,
	const FVector& FacilityLocation,
	const FRotator& FacilityRotation) const
{
	if (CollisionBoxes.IsEmpty())
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
		if (!TryCalculateProspectiveBox(
				CollisionBox,
				CollisionBox->GetOwner(),
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
	CollectBuildPlacementCollisionBoxes(
		HeldActor,
		BuildPlacementCollisionBoxes);
	const bool bIsInsideBuildPlacementArea =
		IsBuildPlacementInsideCachedAreas(
			BuildPlacementCollisionBoxes,
			CachedBuildPlacementLocation,
			CachedBuildPlacementRotation);
	const bool bHasFacilityOverlap =
		HasBuildPlacementCollisionOverlap(
			HeldActor,
			BuildPlacementCollisionBoxes,
			CachedBuildPlacementLocation,
			CachedBuildPlacementRotation);
	bIsHeldBuildableInsidePlacementArea = bIsInsideBuildPlacementArea;
	bHasHeldBuildablePlacementOverlap = bHasFacilityOverlap;
	bCanPlaceHeldBuildableFacility =
		bIsSurfaceValid &&
		bIsInsideBuildPlacementArea &&
		!bHasFacilityOverlap;
	ApplyBuildPlacementPreviewMaterial(bCanPlaceHeldBuildableFacility);
	SetBuildPlacementPreviewVisible(true);
}

void ACSPlayerCharacter::UpdatePlacementPreview()
{
	// 수레가 아닌 대상을 다시 조준하면 이전 수레의 상자 고스트를 먼저 제거합니다.
	ClearCartPlacementPreview();

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
        return;
    }

	if (ACSTrashCanActor* TrashCan = Cast<ACSTrashCanActor>(TargetActor))
	{
		if (TrashCan->CanDisposeItem(HeldActor) && IsValid(StorePlayerController))
		{
			const FText DisposePrompt = Cast<ACSTrashActor>(HeldActor) != nullptr
				? NSLOCTEXT("Interaction", "DisposeTrashPrompt", "[Q] 쓰레기 버리기")
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
		return;
	}

	if (!TargetActor->Implements<UCSShelfPlaceable>())
	{
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

	FTransform PreviewTransform;
	const ECSShelfPlacementPreviewState PreviewState =
		ICSShelfPlaceable::Execute_GetPlacementPreviewState(
			TargetActor, PlacementItem, HitResult.ImpactPoint, PreviewTransform);
	if (PreviewState == ECSShelfPlacementPreviewState::Invalid)
    {
		// 진열대는 조준했지만 빈 슬롯이 없을 때만 가격과 보관 경고 없이 빨간 안내를 표시합니다.
		if (IsValid(StorePlayerController))
		{
			StorePlayerController->ShowPlacementUnavailablePrompt();
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
		if (ACSTrashCanActor* TrashCan = Cast<ACSTrashCanActor>(TargetActor);
			IsValid(TrashCan) && TrashCan->TryDisposeItem(HeldActor))
		{
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

	const FBox Bounds = ActorToDrop->GetComponentsBoundingBox(true);
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
	DestroyPlacementPreview();

	ActorToDrop->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ActorToDrop->SetActorLocation(DropLocation);
	ActorToDrop->SetActorEnableCollision(true);
	if (IsValid(ToolToDrop))
	{
		ToolToDrop->OnUnequipped();
		ToolToDrop->EnableWorldDropPhysics();
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
	// 다른 시설과 겹치면 Area 안팎과 무관하게 설치와 미설치 Drop을 모두 차단합니다.
	if (bHasHeldBuildablePlacementOverlap)
	{
		return;
	}
	if (!bIsHeldBuildableInsidePlacementArea)
	{
		DropHeldBuildableFacilityAsUninstalled();
		return;
	}
	// 향후 다른 불가 조건이 추가되어도 자동으로 미설치 Drop하지 않습니다.
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

void ACSPlayerCharacter::DropHeldBuildableFacilityAsUninstalled()
{
	if (!IsBuildMode() ||
		!bHasBuildPlacementPreview ||
		!IsValid(HeldActor) ||
		!HeldActor->Implements<UCSBuildableFacility>())
	{
		return;
	}

	AActor* FacilityToDrop = HeldActor;
	FacilityToDrop->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	FacilityToDrop->SetActorLocationAndRotation(
		CachedBuildPlacementLocation,
		CachedBuildPlacementRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	RestoreHeldBuildableActorStates();
	FacilityToDrop->SetActorEnableCollision(true);
	ICSBuildableFacility::Execute_InitializeAsUninstalledFacility(FacilityToDrop);

	HeldActor = nullptr;
	ResetBuildRotationInput();
	HeldBuildableFacilityRotation = FRotator::ZeroRotator;
	ClearBuildPlacementPreview();
	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(GetController()))
	{
		StorePlayerController->ClearInteractionContext();
	}
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
		if (!IsValid(ShelfActor) ||
			!ICSShelfPlaceable::Execute_CanPlaceItem(ShelfActor, PlacementItem))
		{
			continue;
		}

		// 외곽선은 특정 단이 아니라 진열대 전체에 빈 슬롯이 있는지를 기준으로 유지합니다.
		const ECSShelfPlacementPreviewState PreviewState =
			ShelfActor->GetPlacementHighlightState(PlacementItem);
		ShelfActor->SetPlacementHighlight(PreviewState);
	}
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
			ShelfActor->SetPlacementHighlight(ECSShelfPlacementPreviewState::Invalid);
		}
	}
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
	if (IsBuildMode())
	{
		if (IsValid(HitActor) && HitActor->Implements<UCSBuildableFacility>())
		{
			TryPickUpBuildableFacility(HitActor);
		}
		return;
	}

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

void ACSPlayerCharacter::ToggleCartPush()
{
	if (IsBuildMode())
	{
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

	if (ACSCartActor* Cart = Cast<ACSCartActor>(HitResult.GetActor());
		IsValid(Cart) && Cart->BeginPushing(this))
	{
		PushedCart = Cart;
	}
}
