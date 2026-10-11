// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerCharacter.h"
#include "Data/CSCollisionChannels.h"
#include "Features/Customers/CSCustomerAnimInstance.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Customers/CSCustomerAIController.h"
#include "Features/Customers/CSCustomerBasketActor.h"
#include "Features/Customers/CSCustomerShoppingBagActor.h"
#include "Data/CSCustomerVisualDataAsset.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "TimerManager.h"
#include "Features//Customers/CSPurchaseDecisionComponent.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	const FName ShoppingBagAttachBoneName(TEXT("RightHand"));
	const FName SubduedHipsBoneName(TEXT("Hips"));
	const FName SubduedSpineConstraintName(TEXT("Spine"));
}

// Sets default values
ACSCustomerCharacter::ACSCustomerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.075f;

	CustomerState = CreateDefaultSubobject<UCSCustomerStateComponent>(TEXT("CustomerState"));
	PurchaseDecision = CreateDefaultSubobject<UCSPurchaseDecisionComponent>(TEXT("PurchaseDecision"));
	BasketAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("BasketAnchor"));
	BasketAnchor->SetupAttachment(GetMesh());
	// 블루프린트에서 고객 메시와 애니메이션에 맞춰 손 위치로 조정합니다.
	BasketAnchor->SetRelativeLocation(FVector(20.0f, 25.0f, 0.0f));

	ShoppingBagAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("ShoppingBagAnchor"));
	ShoppingBagAnchor->SetupAttachment(GetMesh(), ShoppingBagAttachBoneName);
	ShoppingBagAnchor->SetRelativeTransform(FTransform::Identity);


	AIControllerClass = ACSCustomerAIController::StaticClass();// 스폰될 때 어떤 클래스의 AI 컨트롤러가 빙의할지 지정
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned; // 레벨에 배치, 런타임에 SpawnActor로 생성과 상관없이 자동으로 AIController를 빙의하게 함.
	//AIController의 "Start Logic Automatically" 때문에 빙의하자마자 바로 실행됨

	// 이동할 때 자연스럽게 방향을 트는 물리 세팅
	bUseControllerRotationYaw = false; //컨트롤러의 회전을 안받게 false로 설정
	GetCharacterMovement()->bOrientRotationToMovement = true; // 이동하는 방향으로 몸을 자연스럽게 돌림 (걸어가는 방향을 보고 걷게)
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 360.0f, 0.0f); //방향 전환 속도
	GetCharacterMovement()->MaxWalkSpeed = 300.0f; //기본 걷기 속도

	// 플레이어·손님·직원은 서로 통과하고, 월드 충돌은 유지합니다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// 상품을 내려놓기 시작할 기준점. BP에서 메시에 맞게 조정한다.
	DropOriginLeft = CreateDefaultSubobject<UArrowComponent>(TEXT("DropOriginLeft"));
	DropOriginLeft->SetupAttachment(GetCapsuleComponent());
	DropOriginLeft->SetRelativeLocation(FVector(0.0f, -35.0f, -83.0f));
	// 정면 = 열이 넘칠 때 새 열이 생길 방향. 기본은 바깥쪽(왼쪽)
	DropOriginLeft->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	DropOriginLeft->SetHiddenInGame(true);

	DropOriginRight = CreateDefaultSubobject<UArrowComponent>(TEXT("DropOriginRight"));
	DropOriginRight->SetupAttachment(GetCapsuleComponent());
	DropOriginRight->SetRelativeLocation(FVector(0.0f, 35.0f, -83.0f));
	DropOriginRight->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
	DropOriginRight->SetHiddenInGame(true);

	// 구매 거절/가격 반응 시 머리 위에 뜨는 아이콘 - exclamation_point 스태틱 메시로 통일
	HeadIconMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadIconMeshComponent"));
	HeadIconMeshComponent->SetupAttachment(GetCapsuleComponent());
	HeadIconMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 40.0f));
	// 표시 전용 장식 메시라 충돌/그림자는 끔
	HeadIconMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadIconMeshComponent->SetCastShadow(false);
	// Static Mesh가 지정되기 전까지는 빈 메시를 띄우지 않도록 꺼둠
	HeadIconMeshComponent->SetVisibility(false);
}

void ACSCustomerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLocomotionAnimation();
}

void ACSCustomerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bSubduedRagdollActive)
	{
		LogSubduedPhysicsState(TEXT("EndPlay"));
		UE_LOG(LogTemp, Log, TEXT("[제압물리] %s 종료사유=%d"), *GetName(), static_cast<int32>(EndPlayReason));
	}

	if (IsValid(CustomerState))
	{
		CustomerState->OnEnteredStore.Remove(EnteredStoreHandle);
	}
	EnteredStoreHandle.Reset();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ActionAnimationTimerHandle);
		World->GetTimerManager().ClearTimer(HeadIconHideTimerHandle);
		World->GetTimerManager().ClearTimer(PickSoundTimerHandle);
		World->GetTimerManager().ClearTimer(SubduedPhysicsDiagnosticTimerHandle);
	}
	bActionAnimationPlaying = false;
	bHoldActionTransformOnFinish = false;
	bActionTransitionHoldActive = false;
	RestoreActionMeshRotation();

	Super::EndPlay(EndPlayReason);
}

void ACSCustomerCharacter::ShowHeadIcon(bool bShowIcon, float Duration)
{
	if (!IsValid(HeadIconMeshComponent))
	{
		return;
	}

	// [백무송]
	// 빌런으로 배정된 손님은 머리 위 아이콘 표시를 생략
	// 표시 중인 아이콘을 정리하고 튜토리얼 아이콘 표시 알림을 방지
	if (IsValid(CustomerState) && CustomerState->IsVillain())
	{
		HideHeadIcon();
		return;
	}

	// 이 Tier엔 표시할 게 없음(Tiers.Icon이 None) - 기존 표시만 정리
	if (!bShowIcon)
	{
		HideHeadIcon();
		return;
	}

	HeadIconMeshComponent->SetVisibility(true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			HeadIconHideTimerHandle,
			this,
			&ACSCustomerCharacter::HideHeadIcon,
			FMath::Max(Duration, 0.01f),
			false);

		// 튜토리얼의 "속마음이 보입니다" 팁 트리거 - 아이콘이 실제로 표시된 순간에만 알림
		if (UCSCustomerRuntimeSubsystem* Subsystem = World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			Subsystem->NotifyHeadIconShown(this);
		}
	}
}

void ACSCustomerCharacter::HideHeadIcon()
{
	if (IsValid(HeadIconMeshComponent))
	{
		HeadIconMeshComponent->SetVisibility(false);
	}
}

void ACSCustomerCharacter::InitializeFallbackAnimations(
	const UCSCustomerVisualDataAsset* Visual)
{
	if (!IsValid(Visual))
	{
		return;
	}

	FallbackIdleAnimation = Visual->FallbackIdleAnimation;
	FallbackWalkAnimation = Visual->FallbackWalkAnimation;
	FallbackThinkingAnimation = Visual->FallbackThinkingAnimation;
	FallbackPickAnimation = Visual->FallbackPickAnimation;
	ActionMeshWorldZOffset = Visual->ActionMeshWorldZOffset;
	ShoppingBagScaleMultiplier = FMath::Max(0.001f, Visual->ShoppingBagScaleMultiplier);
	ShoppingBagLocationOffset = Visual->ShoppingBagLocationOffset;
	ShoppingBagRotationOffset = Visual->ShoppingBagRotationOffset;
	bFallbackAnimationsInitialized = true;
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		// Native SingleNode subclass: sequence selection and action timers remain in this character.
		MeshComponent->SetAnimInstanceClass(UCSCustomerAnimInstance::StaticClass());
	}
	UpdateLocomotionAnimation(true);
}

bool ACSCustomerCharacter::PlayThinkingAnimation()
{
	return PlayActionAnimation(FallbackThinkingAnimation, true);
}

bool ACSCustomerCharacter::PlayPickAnimation()
{
	if (!PlayActionAnimation(FallbackPickAnimation, false))
	{
		return false;
	}

	// Schedule from the actual played interval, including the existing start-frame skip.
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	Timers.SetTimer(PickSoundTimerHandle, this, &ACSCustomerCharacter::PlayPickSound,
		Timers.GetTimerRemaining(ActionAnimationTimerHandle) * 0.3f, false);
	return true;
}

void ACSCustomerCharacter::PlayPickSound()
{
	if (bActionAnimationPlaying)
	{
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this))
		{
			Audio->PlaySFX(ECSAudioEvent::CustomerPickProduct);
		}
	}
}

bool ACSCustomerCharacter::IsActionAnimationPlaying() const
{
	return bActionAnimationPlaying;
}

void ACSCustomerCharacter::CancelActionAnimation()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ActionAnimationTimerHandle);
		World->GetTimerManager().ClearTimer(PickSoundTimerHandle);
	}

	bActionAnimationPlaying = false;
	bHoldActionTransformOnFinish = false;
	ReleaseActionTransitionHold();
}

void ACSCustomerCharacter::ReleaseActionTransitionHold()
{
	if (bActionAnimationPlaying)
	{
		return;
	}

	const bool bHadHeldActionTransform =
		bActionTransitionHoldActive || bActionMeshRotationApplied;
	bActionTransitionHoldActive = false;
	bHoldActionTransformOnFinish = false;
	if (!bHadHeldActionTransform)
	{
		return;
	}

	UpdateLocomotionAnimation(true);
	RestoreActionMeshRotation();
	RefreshLocomotionAnimationPose();
}

void ACSCustomerCharacter::UpdateLocomotionAnimation(bool bForce)
{
	if (bSubduedRagdollActive || !bFallbackAnimationsInitialized || bActionAnimationPlaying ||
		bActionTransitionHoldActive)
	{
		return;
	}

	// 서로 다른 시작/정지 기준으로 경계 속도에서 Idle/Walk가 반복 전환되지 않게 합니다.
	constexpr float WalkEnterSpeed = 10.0f;
	constexpr float IdleEnterSpeed = 5.0f;
	const float HorizontalSpeed = GetVelocity().Size2D();
	if (bFallbackWalking)
	{
		if (HorizontalSpeed <= IdleEnterSpeed)
		{
			bFallbackWalking = false;
		}
	}
	else if (HorizontalSpeed >= WalkEnterSpeed)
	{
		bFallbackWalking = true;
	}

	UAnimSequence* DesiredAnimation =
		bFallbackWalking ? FallbackWalkAnimation.Get() : FallbackIdleAnimation.Get();
	if (!IsValid(DesiredAnimation))
	{
		DesiredAnimation =
			bFallbackWalking ? FallbackIdleAnimation.Get() : FallbackWalkAnimation.Get();
	}

	if (!IsValid(DesiredAnimation) || (!bForce && CurrentFallbackLoopAnimation == DesiredAnimation))
	{
		return;
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		if (UCSCustomerAnimInstance* Instance = Cast<UCSCustomerAnimInstance>(MeshComponent->GetAnimInstance()))
		{
			// A forced refresh must not restart an already playing Idle/Walk loop.
			if (Instance->GetAnimationAsset() != DesiredAnimation)
			{
				Instance->PlaySequence(DesiredAnimation, true, AnimationTransitionBlendSeconds);
			}
		}
		else
		{
			MeshComponent->PlayAnimation(DesiredAnimation, true);
		}
		CurrentFallbackLoopAnimation = DesiredAnimation;
	}
}

void ACSCustomerCharacter::RefreshLocomotionAnimationPose()
{
	if (!IsValid(CurrentFallbackLoopAnimation))
	{
		return;
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh(); IsValid(MeshComponent))
	{
		MeshComponent->TickAnimation(0.0f, false);
		MeshComponent->RefreshBoneTransforms();
	}
}

bool ACSCustomerCharacter::PlayActionAnimation(
	UAnimSequence* Animation,
	bool bHoldTransformAfterFinish)
{
	if (bActionAnimationPlaying || !IsValid(Animation))
	{
		return false;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	UWorld* World = GetWorld();
	if (!IsValid(MeshComponent) || !IsValid(World))
	{
		return false;
	}

	bActionTransitionHoldActive = false;
	bHoldActionTransformOnFinish = bHoldTransformAfterFinish;
	bActionAnimationPlaying = true;
	if (UCSCustomerAnimInstance* Instance = Cast<UCSCustomerAnimInstance>(MeshComponent->GetAnimInstance()))
	{
		Instance->PlaySequence(Animation, false, AnimationTransitionBlendSeconds);
	}
	else
	{
		MeshComponent->PlayAnimation(Animation, false);
	}
	const float PlayLength = FMath::Max(0.0f, Animation->GetPlayLength());
	const float SafeStartOffset = FMath::Clamp(
		ActionAnimationStartOffsetSeconds,
		0.0f,
		PlayLength);
	MeshComponent->SetPosition(SafeStartOffset, false);
	// Apply the existing mesh correction before evaluating the rebased transition pose.
	ApplyActionMeshRotation();
	MeshComponent->TickAnimation(0.0f, false);
	MeshComponent->RefreshBoneTransforms();

	const float RemainingPlayLength = PlayLength - SafeStartOffset;
	if (RemainingPlayLength <= 0.0f)
	{
		// 재생할 구간이 없으면 Hold를 남기지 않고 기존 누락 애니메이션 폴백으로 복귀합니다.
		bHoldActionTransformOnFinish = false;
		HandleActionAnimationFinished();
		return false;
	}

	World->GetTimerManager().SetTimer(
		ActionAnimationTimerHandle,
		this,
		&ACSCustomerCharacter::HandleActionAnimationFinished,
		RemainingPlayLength,
		false);
	return true;
}

void ACSCustomerCharacter::HandleActionAnimationFinished()
{
	bActionAnimationPlaying = false;
	if (bHoldActionTransformOnFinish)
	{
		bHoldActionTransformOnFinish = false;
		bActionTransitionHoldActive = true;
		return;
	}

	ReleaseActionTransitionHold();
}

void ACSCustomerCharacter::ApplyActionMeshRotation()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (bActionMeshRotationApplied || !IsValid(MeshComponent))
	{
		return;
	}

	SavedActionMeshRelativeTransform = MeshComponent->GetRelativeTransform();

	FTransform BasketAnchorWorldTransform = FTransform::Identity;
	bActionBasketAnchorTransformCached = IsValid(BasketAnchor);
	if (bActionBasketAnchorTransformCached)
	{
		SavedActionBasketAnchorRelativeTransform = BasketAnchor->GetRelativeTransform();
		BasketAnchorWorldTransform = BasketAnchor->GetComponentTransform();
	}

	// AddLocalRotation의 Quaternion 오버로드로 현재 Mesh의 로컬 X축 기준 -90도를 합성합니다.
	const FQuat LocalXRotationOffset(
		FVector::ForwardVector,
		FMath::DegreesToRadians(-90.0f));
	MeshComponent->AddLocalRotation(
		LocalXRotationOffset,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	MeshComponent->AddWorldOffset(
		FVector(0.0f, 0.0f, ActionMeshWorldZOffset),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	bActionMeshRotationApplied = true;

	if (bActionBasketAnchorTransformCached && IsValid(BasketAnchor))
	{
		// Basket/ShoppingBag은 Mesh의 자식이므로 액션 Transform 보정을 상속하지 않도록 기존 월드 자세를 유지합니다.
		BasketAnchor->SetWorldTransform(
			BasketAnchorWorldTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}
}

void ACSCustomerCharacter::RestoreActionMeshRotation()
{
	if (!bActionMeshRotationApplied)
	{
		return;
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh(); IsValid(MeshComponent))
	{
		MeshComponent->SetRelativeTransform(
			SavedActionMeshRelativeTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	if (bActionBasketAnchorTransformCached && IsValid(BasketAnchor))
	{
		BasketAnchor->SetRelativeTransform(
			SavedActionBasketAnchorRelativeTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	bActionMeshRotationApplied = false;
	bActionBasketAnchorTransformCached = false;
}

void ACSCustomerCharacter::BeginPlay()
{
	Super::BeginPlay();
	RefreshArtifactMoveSpeed();

	// 행인은 장바구니를 갖지 않는다. 입장이 확정되는 순간에만 생성
	if (IsValid(CustomerState))
	{
		EnteredStoreHandle = CustomerState->OnEnteredStore.AddUObject(
			this,
			&ACSCustomerCharacter::HandleEnteredStore);

		// 이미 입장 상태로 시작한 경우(디버그 배치 등) 대비
		if (CustomerState->HasEnteredStore())
		{
			EnsureBasketSpawned();
		}
	}
}

void ACSCustomerCharacter::RefreshArtifactMoveSpeed()
{
	const UCSArtifactSubsystem* Artifacts = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>() : nullptr;
	const float Multiplier = Artifacts ? Artifacts->GetSpeedMultiplier(ECSArtifactEffectType::CustomerSpeed) : 1.0f;
	GetCharacterMovement()->MaxWalkSpeed *= Multiplier / FMath::Max(LastArtifactSpeedMultiplier, 0.01f);
	LastArtifactSpeedMultiplier = Multiplier;
}

void ACSCustomerCharacter::HandleEnteredStore(UCSCustomerStateComponent* State)
{
	EnsureBasketSpawned();
}

void ACSCustomerCharacter::EnsureBasketSpawned()
{
	UWorld* World = GetWorld();
	if (IsValid(BasketActor) || BasketActorClass == nullptr || World == nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 장바구니는 손님에게 Attach하지 않고 월드에 따로 서서 따라다닙니다.
	// 위치는 StartFollowing에서 Park 자리를 찾아 순간이동하므로 우선 손님 발 위치에 만듭니다.
	const FVector FootLocation = GetActorLocation() -
		FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	BasketActor = World->SpawnActor<ACSCustomerBasketActor>(
		BasketActorClass,
		FootLocation,
		FRotator(0.0f, GetActorRotation().Yaw, 0.0f),
		SpawnParams);

	if (IsValid(BasketActor))
	{
		BasketActor->StartFollowing(this);
	}
}

UCSCustomerStateComponent* ACSCustomerCharacter::GetCustomerState() const
{
	return CustomerState;
}

void ACSCustomerCharacter::AddBasketDisplayProduct(
	const FCSSelectProduct& SelectedProduct)
{
	// 안전망: 입장 확정 경로를 거치지 않은 경우에도 상품 표시가 사라지지 않게 합니다.
	EnsureBasketSpawned();
	if (IsValid(BasketActor))
	{
		BasketActor->AddDisplayProduct(SelectedProduct);
	}
}

void ACSCustomerCharacter::ClearBasketDisplayProducts()
{
	if (IsValid(BasketActor))
	{
		BasketActor->ClearDisplayProducts();
	}
}

void ACSCustomerCharacter::SetBasketDisplayVisible(bool bShouldBeVisible)
{
	if (IsValid(BasketActor))
	{
		BasketActor->SetBasketDisplayVisible(bShouldBeVisible);
	}
}

void ACSCustomerCharacter::ShowShoppingBag()
{
	SetBasketDisplayVisible(false);
	if (IsValid(ShoppingBagActor) ||
		ShoppingBagActorClass == nullptr ||
		GetWorld() == nullptr ||
		!IsValid(ShoppingBagAnchor))
	{
		return;
	}

	const USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!IsValid(MeshComponent) || MeshComponent->GetBoneIndex(ShoppingBagAttachBoneName) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: ShoppingBag was not spawned because the current skeletal mesh has no RightHand bone."),
			*GetName());
		return;
	}

	FTransform SpawnTransform = ShoppingBagAnchor->GetComponentTransform();
	// 생성 시에도 손님과 Mesh의 스케일을 복사하지 않아 종족에 관계없이 봉투 크기를 유지합니다.
	SpawnTransform.SetScale3D(FVector::OneVector);
	ShoppingBagActor = GetWorld()->SpawnActor<ACSCustomerShoppingBagActor>(
		ShoppingBagActorClass,
		SpawnTransform);
	if (IsValid(ShoppingBagActor))
	{
		ShoppingBagActor->AttachToComponent(
			ShoppingBagAnchor,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		// 결제 후 생성하는 쇼핑백도 손님 스케일을 상속하지 않도록 같은 규칙을 적용합니다.
		ShoppingBagActor->GetRootComponent()->SetAbsolute(false, false, true);
		// 신규 모델만 Visual에서 단위 차이를 보정합니다. 기존 종족은 기본값 1로 동작이 같습니다.
		ShoppingBagActor->SetActorScale3D(ShoppingBagActor->GetActorScale3D() * ShoppingBagScaleMultiplier);
		ShoppingBagActor->GetRootComponent()->SetRelativeLocationAndRotation(ShoppingBagLocationOffset, ShoppingBagRotationOffset);
	}
}

void ACSCustomerCharacter::ApplyMagicHit_Implementation(
	FVector HitLocation,
	FVector ImpactDirection,
	float KnockbackStrength)
{
	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* Subsystem = IsValid(World)
		? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		return;
	}
	const bool bSubdued = Subsystem->HandleMagicHitOnCustomer(this, ImpactDirection, KnockbackStrength);
	if (bSubdued)
	{
		return;
	}

	// 제압 대상이 아닌 손님의 최근 구매 반응만 기존 공용 위젯에 표시
	// [백무송]
	// 빌런으로 배정된 손님은 속마음 표시를 생략
	if (UCSCustomerStateComponent* State = GetCustomerState();
		State != nullptr && !State->IsVillain() && IsValid(PurchaseDecision) &&
		State->LastRejectionInfo.Reason != ECSPurchaseRejectReason::None &&
		(World->GetTimeSeconds() - State->LastRejectionTime) <=
		PurchaseDecision->GetInstantDisplayDuration(State->LastRejectionInfo.Reason))
	{
		Subsystem->ShowInnerThought(InnerThoughtWidgetClass,
			PurchaseDecision->BuildInnerThoughtText(State->LastRejectionInfo), 4.0f);
	}
}

void ACSCustomerCharacter::QueueSubduedReaction(
	const FVector& ImpactDirection,
	float KnockbackStrength)
{
	if (bSubduedReactionPending || bSubduedRagdollActive)
	{
		return;
	}

	PendingSubduedImpactDirection = ImpactDirection;
	PendingSubduedKnockbackStrength = KnockbackStrength;
	bSubduedReactionPending = true;
}

void ACSCustomerCharacter::BeginSubduedReaction()
{
	if (!bSubduedReactionPending || bSubduedRagdollActive)
	{
		return;
	}

	const FVector ImpactDirection = PendingSubduedImpactDirection;
	const float KnockbackStrength = PendingSubduedKnockbackStrength;
	// 물리 준비에 실패해도 같은 피격을 다시 적용하지 않고 기존 소멸 대기를 유지
	bSubduedReactionPending = false;
	PendingSubduedImpactDirection = FVector::ZeroVector;
	PendingSubduedKnockbackStrength = 0.0f;

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!IsValid(MeshComponent) || MeshComponent->GetPhysicsAsset() == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제압] %s 래그돌 Physics Asset 없음"), *GetName());
		return;
	}

	const int32 HipsBoneIndex = MeshComponent->GetBoneIndex(SubduedHipsBoneName);
	if (HipsBoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제압] %s 골반 본 없음"), *GetName());
		return;
	}

	// RecreatePhysicsState는 등록된 컴포넌트의 동기 물리 상태를 대상으로 한다.
	if (!MeshComponent->IsRegistered() || MeshComponent->IsAsyncCreatePhysicsStateRunning() ||
		MeshComponent->IsAsyncDestroyPhysicsStateRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[제압] %s 물리 상태 준비 중: 래그돌 생략, 소멸 대기 유지"), *GetName());
		return;
	}

	// 이전 프레임의 병렬 평가가 물리 재생성 뒤에 오래된 자세를 덮어쓰지 않게 완료한다.
	MeshComponent->HandleExistingParallelEvaluationTask(true, true);
	const FTransform RagdollMeshWorldTransform = MeshComponent->GetComponentTransform();
	SubduedReactionStartTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f;

	// 행동 취소 중 Idle/Walk 재시작을 막고 피격 순간의 메시 월드 변환을 유지한다.
	// Thinking/Pick의 회전·높이 보정만 제거하면 같은 본 자세가 바닥 아래로 옮겨질 수 있다.
	bSubduedRagdollActive = true;
	CancelActionAnimation();
	RestoreActionMeshRotation();
	MeshComponent->SetWorldTransform(RagdollMeshWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	HideHeadIcon();
	SetBasketDisplayVisible(false);
	if (IsValid(ShoppingBagActor))
	{
		ShoppingBagActor->SetActorHiddenInGame(true);
	}
	// 현재 애니메이션 단위(다른 5종의 Hips 0.01, Dwarf 1)를 동기 평가한다.
	// 이 설정은 제압된 메시의 남은 수명에만 적용하며 물리 결과의 본 갱신은 계속 허용한다.
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetAllBodiesPhysicsBlendWeight(0.0f, false);
	MeshComponent->SetComponentTickEnabled(true);
	MeshComponent->bEnableAnimation = true;
	MeshComponent->bNoSkeletonUpdate = false;
	MeshComponent->bEnableUpdateRateOptimizations = false;
	MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	MeshComponent->bPauseAnims = false;
	MeshComponent->TickAnimation(0.0f, false);
	MeshComponent->RefreshBoneTransforms();
	MeshComponent->HandleExistingParallelEvaluationTask(true, true);
	MeshComponent->bPauseAnims = true;

	const FTransform HipsWorldTransform = MeshComponent->GetBoneTransform(HipsBoneIndex);
	if (HipsWorldTransform.ContainsNaN() || HipsWorldTransform.GetScale3D().GetMin() <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제압] %s 골반 자세·스케일 오류: 래그돌 생략, 소멸 대기 유지"), *GetName());
		return;
	}

	LogSubduedPhysicsState(TEXT("BeforeRecreate"));
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCollisionObjectType(CSCollisionChannels::SubduedBody);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeshComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// 몸체 크기만 애니메이션으로 줄고 관절 연결 위치는 참조 자세 크기로 남는 경로를 제거한다.
	// 엔진이 현재 본 변환으로 몸체와 관절 연결 위치를 함께 준비하도록 다시 생성한다.
	MeshComponent->RecreatePhysicsState();
	FBodyInstance* HipsBody = MeshComponent->GetBodyInstance(SubduedHipsBoneName);
	if (HipsBody == nullptr || !HipsBody->IsValidBodyInstance())
	{
		UE_LOG(LogTemp, Warning, TEXT("[제압] %s 물리 재생성 후 골반 물리 몸체 없음"), *GetName());
		return;
	}

	LogSubduedPhysicsState(TEXT("AfterRecreate"));
	MeshComponent->SetAllBodiesCollisionObjectType(CSCollisionChannels::SubduedBody);
	MeshComponent->SetSimulatePhysics(true);
	if (!MeshComponent->IsSimulatingPhysics(SubduedHipsBoneName))
	{
		MeshComponent->SetSimulatePhysics(false);
		UE_LOG(LogTemp, Warning, TEXT("[제압] %s 골반 물리 활성화 실패: 소멸 대기 유지"), *GetName());
		return;
	}

	// 시뮬레이션 전환이 가져온 기존 운동량을 제거한 뒤 최초 피격 충격만 더한다.
	MeshComponent->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
	MeshComponent->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
	MeshComponent->SetAllBodiesPhysicsBlendWeight(1.0f, false);
	MeshComponent->WakeAllRigidBodies();

	// 종족별 질량 차이를 없애고 투사체 진행 방향으로 골반에 한 번만 충격을 적용
	FVector HorizontalDirection = FVector(ImpactDirection.X, ImpactDirection.Y, 0.0f).GetSafeNormal();
	if (HorizontalDirection.IsNearlyZero())
	{
		HorizontalDirection = -GetActorForwardVector().GetSafeNormal2D();
	}
	const float HorizontalVelocityChange = FMath::Min(
		FMath::Max(0.0f, KnockbackStrength) * FMath::Max(0.0f, SubduedHorizontalVelocityScale),
		FMath::Max(0.0f, SubduedMaxHorizontalVelocityChange));
	const FVector VelocityChange = HorizontalDirection * HorizontalVelocityChange +
		FVector(0.0f, 0.0f, FMath::Max(0.0f, SubduedUpwardVelocityChange));
	MeshComponent->AddImpulse(VelocityChange, SubduedHipsBoneName, true);
	MeshComponent->UpdateBounds();
	LogSubduedPhysicsState(TEXT("Started"));
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(SubduedPhysicsDiagnosticTimerHandle, this,
			&ACSCustomerCharacter::LogSubduedPhysicsAfterSimulation, 0.1f, false);
	}
}

void ACSCustomerCharacter::LogSubduedPhysicsState(const TCHAR* Stage) const
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!IsValid(MeshComponent))
	{
		return;
	}

	const FBodyInstance* HipsBody = MeshComponent->GetBodyInstance(SubduedHipsBoneName);
	const bool bHasHipsBody = HipsBody != nullptr && HipsBody->IsValidBodyInstance();
	const float Elapsed = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() - SubduedReactionStartTime : 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[제압물리] %s 단계=%s 경과=%.3f Mesh=%s HipsBody=%d Sim=%d MeshScale=%s BoneScale=%s BodyScale=%s COM=%s Velocity=%s AngularVelocity=%s BoundsExtent=%s"),
		*GetName(), Stage, Elapsed, *GetNameSafe(MeshComponent->GetSkeletalMeshAsset()),
		bHasHipsBody, MeshComponent->IsSimulatingPhysics(SubduedHipsBoneName),
		*MeshComponent->GetComponentScale().ToString(),
		*MeshComponent->GetSocketTransform(SubduedHipsBoneName, RTS_World).GetScale3D().ToString(),
		*(bHasHipsBody ? HipsBody->Scale3D : FVector::ZeroVector).ToString(),
		*(bHasHipsBody ? HipsBody->GetCOMPosition() : FVector::ZeroVector).ToString(),
		*MeshComponent->GetPhysicsLinearVelocity(SubduedHipsBoneName).ToString(),
		*MeshComponent->GetPhysicsAngularVelocityInRadians(SubduedHipsBoneName).ToString(),
		*MeshComponent->GetBounds().BoxExtent.ToString());

	if (const FConstraintInstance* SpineConstraint = MeshComponent->FindConstraintInstance(SubduedSpineConstraintName))
	{
		UE_LOG(LogTemp, Log, TEXT("[제압물리] %s 단계=%s SpinePos1=%s SpinePos2=%s"),
			*GetName(), Stage, *SpineConstraint->Pos1.ToString(), *SpineConstraint->Pos2.ToString());
	}
}

void ACSCustomerCharacter::LogSubduedPhysicsAfterSimulation()
{
	if (bSubduedRagdollActive)
	{
		LogSubduedPhysicsState(TEXT("AfterSimulation"));
	}
}

void ACSCustomerCharacter::SetVillainHighlight(bool bEnabled)
{
	if (bEnabled == false)
	{
		// 백업해 둔 원래 렌더링 설정으로 복구
		for (const TPair<TObjectPtr<UPrimitiveComponent>, bool>& Pair : OriginalCustomDepthStates)
		{
			UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get();
			if (!IsValid(PrimitiveComponent))
			{
				continue;
			}

			PrimitiveComponent->SetRenderCustomDepth(Pair.Value);
			if (const int32* OriginalStencilValue =
				OriginalCustomStencilValues.Find(PrimitiveComponent))
			{
				PrimitiveComponent->SetCustomDepthStencilValue(*OriginalStencilValue);
			}
		}

		OriginalCustomDepthStates.Empty();
		OriginalCustomStencilValues.Empty();
		return;
	}

	// 장바구니는 손님을 따라다니는 별개 액터라서 여기에 포함되지 않음.
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		// 최초 표시 시의 값만 저장하여 외곽선 해제 후 원래 설정으로 되돌림
		if (!OriginalCustomDepthStates.Contains(PrimitiveComponent))
		{
			OriginalCustomDepthStates.Add(PrimitiveComponent,PrimitiveComponent->bRenderCustomDepth);
			OriginalCustomStencilValues.Add(PrimitiveComponent,PrimitiveComponent->CustomDepthStencilValue);
		}

		PrimitiveComponent->SetRenderCustomDepth(true);
		PrimitiveComponent->SetCustomDepthStencilValue(VillainOutlineStencilValue);
	}
}

const UArrowComponent* ACSCustomerCharacter::GetDropOrigin(const int32 SideIndex) const
{
	if (SideIndex == 0)
	{
		return DropOriginLeft;
	}
	else
	{
		return DropOriginRight;
	}
}

void ACSCustomerCharacter::Destroyed()
{
	ClearBasketDisplayProducts();
	if (IsValid(BasketActor))
	{
		BasketActor->Destroy();
	}
	if (IsValid(ShoppingBagActor))
	{
		ShoppingBagActor->Destroy();
	}
	AController* MyController = GetController(); //현재 컨트롤러를 미리 변수에 저장

	Super::Destroyed();

	if (MyController != nullptr)
	{
		MyController->Destroy(); //폰이 사라져도 컨트롤러가 레벨에 혼자 남지 않게 정리
	}
}

