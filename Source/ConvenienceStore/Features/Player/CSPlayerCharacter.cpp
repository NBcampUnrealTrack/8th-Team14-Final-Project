#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "InputActionValue.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"

ACSPlayerCharacter::ACSPlayerCharacter()
{
    // 진열 프리뷰는 물체를 들고 있을 때만 갱신합니다.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    // 캐릭터 몸체는 회전하지 않고, 카메라 시점만 회전시킵니다.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = true;
    bUseControllerRotationRoll = false;

    // 이동 방향으로 자동 회전하지 않게 합니다.
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed = 350.0f;

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
    GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
}

void ACSPlayerCharacter::StopSprint()
{
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
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
    UpdatePlacementPreview();
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

    PickupActor->SetActorEnableCollision(false);
    PickupActor->AttachToComponent(
        HeldItemAnchor,
        FAttachmentTransformRules::SnapToTargetNotIncludingScale);

    HeldActor = PickupActor;
    SetActorTickEnabled(true);
    return true;
}

void ACSPlayerCharacter::UpdatePlacementPreview()
{
    if (!IsValid(HeldActor) || FirstPersonCamera == nullptr)
    {
        SetActorTickEnabled(false);
        return;
    }

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

    FTransform PreviewTransform;
    if (!ICSShelfPlaceable::Execute_GetPlacementPreview(
        TargetActor, HeldActor, HitResult.ImpactPoint, PreviewTransform))
    {
        return;
    }

    const FVector ItemExtent = HeldActor->GetComponentsBoundingBox(false).GetExtent();
    DrawDebugBox(
        GetWorld(),
        PreviewTransform.GetLocation(),
        ItemExtent.ComponentMax(FVector(5.0f)),
        PreviewTransform.GetRotation(),
        FColor::Green,
        false,
        0.0f,
        0,
        2.0f);
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
            IsValid(TargetActor) && TargetActor->Implements<UCSShelfPlaceable>() &&
            ICSShelfPlaceable::Execute_TryPlaceItem(
                TargetActor,
                HeldActor,
                AimHitResult.ImpactPoint))
        {
            HeldActor = nullptr;
            SetActorTickEnabled(false);
            return;
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
    HeldActor = nullptr;

    ActorToDrop->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    // 액터 중심이 아닌 바닥면이 표면에 닿도록 높이를 보정합니다.
    FVector DropLocation = HitResult.ImpactPoint;
    DropLocation.Z += ActorToDrop->GetComponentsBoundingBox(false).GetExtent().Z;

    ActorToDrop->SetActorLocation(DropLocation);
    ActorToDrop->SetActorEnableCollision(true);
    SetActorTickEnabled(false);
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
