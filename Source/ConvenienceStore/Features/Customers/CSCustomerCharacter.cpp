// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerCharacter.h"
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
#include "Engine/World.h"
#include "TimerManager.h"
#include "Features//Customers/CSPurchaseDecisionComponent.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"

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
}

void ACSCustomerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLocomotionAnimation();
}

void ACSCustomerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ActionAnimationTimerHandle);
	}
	bActionAnimationPlaying = false;
	bHoldActionTransformOnFinish = false;
	bActionTransitionHoldActive = false;
	RestoreActionMeshRotation();

	Super::EndPlay(EndPlayReason);
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
	bFallbackAnimationsInitialized = true;
	UpdateLocomotionAnimation(true);
}

bool ACSCustomerCharacter::PlayThinkingAnimation()
{
	return PlayActionAnimation(FallbackThinkingAnimation, true);
}

bool ACSCustomerCharacter::PlayPickAnimation()
{
	return PlayActionAnimation(FallbackPickAnimation, false);
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
	RefreshLocomotionAnimationPose();
	RestoreActionMeshRotation();
}

void ACSCustomerCharacter::UpdateLocomotionAnimation(bool bForce)
{
	if (!bFallbackAnimationsInitialized || bActionAnimationPlaying ||
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
		MeshComponent->PlayAnimation(DesiredAnimation, true);
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
		MeshComponent->SetPosition(0.0f, false);
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
	MeshComponent->PlayAnimation(Animation, false);
	const float PlayLength = FMath::Max(0.0f, Animation->GetPlayLength());
	const float SafeStartOffset = FMath::Clamp(
		ActionAnimationStartOffsetSeconds,
		0.0f,
		PlayLength);
	MeshComponent->SetPosition(SafeStartOffset, false);
	// Single Node의 첫 Pose를 같은 게임 프레임에 평가한 뒤 Mesh Transform을 보정합니다.
	MeshComponent->TickAnimation(0.0f, false);
	MeshComponent->RefreshBoneTransforms();
	ApplyActionMeshRotation();

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

	if (BasketActorClass == nullptr || GetWorld() == nullptr || !IsValid(BasketAnchor))
	{
		return;
	}

	BasketActor = GetWorld()->SpawnActor<ACSCustomerBasketActor>(
		BasketActorClass,
		BasketAnchor->GetComponentTransform());
	if (IsValid(BasketActor))
	{
		BasketActor->AttachToComponent(
			BasketAnchor,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		// 손님 크기가 바뀌어도 장바구니는 원래 크기를 유지하고 위치·회전만 따라가도록 설정합니다.
		BasketActor->GetRootComponent()->SetAbsolute(false, false, true);
	}
}

UCSCustomerStateComponent* ACSCustomerCharacter::GetCustomerState() const
{
	return CustomerState;
}

void ACSCustomerCharacter::AddBasketDisplayProduct(
	const FCSSelectProduct& SelectedProduct)
{
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
		!IsValid(BasketAnchor))
	{
		return;
	}

	ShoppingBagActor = GetWorld()->SpawnActor<ACSCustomerShoppingBagActor>(
		ShoppingBagActorClass,
		BasketAnchor->GetComponentTransform());
	if (IsValid(ShoppingBagActor))
	{
		ShoppingBagActor->AttachToComponent(
			BasketAnchor,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		// 결제 후 생성하는 쇼핑백도 손님 스케일을 상속하지 않도록 같은 규칙을 적용합니다.
		ShoppingBagActor->GetRootComponent()->SetAbsolute(false, false, true);
	}
}

void ACSCustomerCharacter::ApplyMagicHit_Implementation(
	FVector HitLocation,
	FVector ImpactDirection,
	float KnockbackStrength)
{
	// 공격 판정을 받았을 때 Subsystem에서 빌런인지 확인
	UWorld* World = GetWorld();
    UCSCustomerRuntimeSubsystem* Subsystem = IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
    if (Subsystem != nullptr)
    {
        Subsystem->HandleMagicHitOnCustomer(this, ImpactDirection, KnockbackStrength);
    }

	// ==== 트리거 지점: 지금은 마법봉 히트, 나중에 다른 트리거로 바뀔 수 있음 ====
	// Subsystem->ShowInnerThought()/Decision->BuildInnerThoughtText는 트리거가 뭐든 그대로 재사용됨
	// 트리거를 옮길 때는 이 블록만 옮기면 되고, 저 두 함수는 안 건드림
	// 위젯 자체(화면 하단 대화창)는 손님별로 안 만들고 Subsystem이 인스턴스 하나를 캐싱해서 재사용함
	if (Subsystem != nullptr)
	{
		UCSCustomerStateComponent* State = GetCustomerState();
		// 실제 거절이든(안 삼) 가격 반응은 있었지만 결국 사는 케이스든 손님이 똑같이 멈춰서 있는
		// 동안이라 대기시간 계산도 동일하게 GetInstantDisplayDuration 하나만 보면 됨
		if (State != nullptr && IsValid(PurchaseDecision) &&
			State->LastRejectionInfo.Reason != ECSPurchaseRejectReason::None &&
			(World->GetTimeSeconds() - State->LastRejectionTime) <= PurchaseDecision->GetInstantDisplayDuration(State->LastRejectionInfo.Reason))
		{
			Subsystem->ShowInnerThought(InnerThoughtWidgetClass, PurchaseDecision->BuildInnerThoughtText(State->LastRejectionInfo), 4.0f);
		}
	}
	// ==== 트리거 지점 끝 ====

	// 투사체가 배 쪽에서 들어오면 등 방향으로 밀리도록 공격 진행 방향을 사용합니다.
	FVector LaunchVelocity = ImpactDirection.GetSafeNormal() * FMath::Max(0.0f, KnockbackStrength);
	LaunchVelocity.Z += MagicKnockbackUpwardStrength;

	// 넉백 연출을 점검하기 전까지는 실제 이동 대신 피격 정보만 출력합니다.
	const FString DebugMessage = FString::Printf(
		TEXT("[Magic Hit] Target: %s | Strength: %.0f | Direction: %s"),
		*GetName(),
		KnockbackStrength,
		*LaunchVelocity.GetSafeNormal().ToCompactString());
	UE_LOG(LogTemp, Warning, TEXT("%s"), *DebugMessage);
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			2.0f,
			FColor::Cyan,
			DebugMessage);
	}

	// LaunchCharacter(LaunchVelocity, true, true);
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

	// 손님에 Attach된 장바구니는 별개 액터라서 여기에 포함되지 않음.
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

