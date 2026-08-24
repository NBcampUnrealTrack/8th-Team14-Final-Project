// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Customers/CSCustomerAIController.h"
#include "Features/Customers/CSCustomerBasketActor.h"
#include "Features/Customers/CSCustomerShoppingBagActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features//Customers/CSPurchaseDecisionComponent.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"

// Sets default values
ACSCustomerCharacter::ACSCustomerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

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

	//일단 서로 겹치게 테스트용으로 잘 굴러가는지 확인용 [나중에 처리 예정]
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
	// 공격 판정을 받았을 때 Subsystem에서 빌런
	UWorld* World = GetWorld();
    if (IsValid(World))
    {
        UCSCustomerRuntimeSubsystem* Subsystem = World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
        if (Subsystem != nullptr)
        {
            Subsystem->HandleMagicHitOnCustomer(this, ImpactDirection, KnockbackStrength);
        }
    }

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

