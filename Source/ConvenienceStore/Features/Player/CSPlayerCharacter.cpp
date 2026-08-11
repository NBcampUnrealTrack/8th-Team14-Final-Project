#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Data/CSProductTypes.h"

#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "InputActionValue.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"

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
    GetCharacterMovement()->MaxWalkSpeed = 350.0f;

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

    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    // 웅크린 상태에서는 일반 걷기보다 느린 이동 속도를 적용합니다.
    GetCharacterMovement()->MaxWalkSpeedCrouched = CrouchSpeed;
    // 점프 높이를 결정합니다.
    GetCharacterMovement()->JumpZVelocity = 420.0f;
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
}

void ACSPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

    EnhancedInputComponent->BindAction(
        ToggleClockAction, ETriggerEvent::Started, this,
        &ACSPlayerCharacter::ToggleClock);
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

void ACSPlayerCharacter::ToggleClock()
{
    if (ACSStorePlayerController* StorePlayerController =
        Cast<ACSStorePlayerController>(GetController()))
    {
        StorePlayerController->ToggleClock();
    }
}

void ACSPlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

	if (IsValid(HeldActor))
	{
		UpdatePlacementPreview();
	}
	else
	{
		UpdateInteractionHUD();
	}
}

bool ACSPlayerCharacter::TryPickUp(AActor* PickupActor)
{
    if (!IsValid(PickupActor) || IsValid(HeldActor) || HeldItemAnchor == nullptr)
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
	}
	else if (ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(PickupActor))
	{
		DeliveryCrate->PrepareForPickupPhysics();
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
	if (!IsValid(TargetActor) || !TargetActor->Implements<UCSInteractable>())
	{
		return;
	}

	const FText InteractionText = ICSInteractable::Execute_GetInteractionText(TargetActor);
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
	}
	else if (const ACSDeliveryCrateActor* DeliveryCrate =
		Cast<ACSDeliveryCrateActor>(TargetActor))
	{
		ShowProductShelfLife(DeliveryCrate->GetProductRuntimeState());
	}
}

void ACSPlayerCharacter::UpdatePlacementPreview()
{
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
    if (!IsValid(TargetActor) || !TargetActor->Implements<UCSShelfPlaceable>())
    {
        return;
    }

	AActor* PlacementItem = HeldActor;
	if (Cast<ACSDeliveryCrateActor>(HeldActor) != nullptr)
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
    if (!IsValid(HeldActor) || FirstPersonCamera == nullptr)
    {
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
		if (AActor* TargetActor = AimHitResult.GetActor();
			IsValid(TargetActor) && TargetActor->Implements<UCSShelfPlaceable>())
		{
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

	// Bounds 계산 전에 물리를 끄고 메시를 정상 SceneRoot/상대 Transform 상태로 맞춥니다.
	if (IsValid(ProductToDrop))
	{
		ProductToDrop->PrepareForPickupPhysics();
	}
	else if (IsValid(CrateToDrop))
	{
		CrateToDrop->PrepareForPickupPhysics();
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

	// 최종 안전 위치로 이동한 뒤 실제 월드 Drop 대상의 메시 물리만 활성화합니다.
	if (IsValid(ProductToDrop))
	{
		ProductToDrop->EnableWorldDropPhysics();
	}
	else if (IsValid(CrateToDrop))
	{
		CrateToDrop->EnableWorldDropPhysics();
	}

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

		FTransform UnusedSlotTransform;
		const ECSShelfPlacementPreviewState PreviewState =
			ICSShelfPlaceable::Execute_GetPlacementPreviewState(
				ShelfActor,
				PlacementItem,
				ShelfActor->GetActorLocation(),
				UnusedSlotTransform);
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

    if (IsValid(HitActor) && HitActor->Implements<UCSInteractable>())
    {
        ICSInteractable::Execute_Interact(HitActor, this);
    }
}
