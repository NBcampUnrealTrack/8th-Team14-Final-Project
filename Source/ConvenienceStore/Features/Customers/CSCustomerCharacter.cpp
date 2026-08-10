// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Customers/CSCustomerAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"

// Sets default values
ACSCustomerCharacter::ACSCustomerCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	CustomerState = CreateDefaultSubobject<UCSCustomerStateComponent>(TEXT("CustomerState"));


	AIControllerClass = ACSCustomerAIController::StaticClass();// 스폰될 때 어떤 클래스의 AI 컨트롤러가 빙의할지 지정
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned; // 레벨에 배치, 런타임에 SpawnActor로 생성과 상관없이 자동으로 AIController를 빙의하게 함.
	//AIController의 "Start Logic Automatically" 때문에 빙의하자마자 바로 실행됨

	// 이동할 때 자연스럽게 방향을 트는 물리 세팅
	bUseControllerRotationYaw = false; //컨트롤러의 회전을 안받게 false로 설정
	GetCharacterMovement()->bOrientRotationToMovement = true; // 이동하는 방향으로 몸을 자연스럽게 돌림 (걸어가는 방향을 보고 걷게)
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 360.0f, 0.0f); //방향 전환 속도
	GetCharacterMovement()->MaxWalkSpeed = 250.0f; //기본 걷기 속도

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

UCSCustomerStateComponent* ACSCustomerCharacter::GetCustomerState() const
{
	return CustomerState;
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
	AController* MyController = GetController(); //현재 컨트롤러를 미리 변수에 저장

	Super::Destroyed();

	if (MyController != nullptr)
	{
		MyController->Destroy(); //폰이 사라져도 컨트롤러가 레벨에 혼자 남지 않게 정리
	}
}

