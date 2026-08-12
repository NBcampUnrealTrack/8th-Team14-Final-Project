#include "Features/Store/CSStoreSignboardActor.h"

#include "Blueprint/UserWidget.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Store/CSStoreBusinessConfirmWidget.h"

ACSStoreSignboardActor::ACSStoreSignboardActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SignboardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SignboardMesh"));
	SetRootComponent(SignboardMesh);
	SignboardMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSStoreSignboardActor::BeginPlay()
{
	Super::BeginPlay();

	// 레벨에 배치한 현재 방향을 Close 면이 보이는 기본 방향으로 사용합니다.
	ClosedWorldRotation = GetActorRotation();

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnStoreOpenStateChanged.AddDynamic(
			this, &ACSStoreSignboardActor::HandleStoreOpenStateChanged);

		// 게임 시작 시에는 애니메이션 없이 현재 영업 상태에 맞는 면을 바로 표시합니다.
		UpdateSignboardRotation(true);
	}
}

void ACSStoreSignboardActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsSignboardRotating)
	{
		SetActorTickEnabled(false);
		return;
	}

	// 입간판이 목표 각도까지 일정한 속도로 자연스럽게 회전하도록 처리합니다.
	const FRotator NewRotation = FMath::RInterpConstantTo(
		GetActorRotation(), TargetSignboardRotation, DeltaTime, SignboardRotationSpeed);
	SetActorRotation(NewRotation);

	if (NewRotation.Equals(TargetSignboardRotation, 0.1f))
	{
		SetActorRotation(TargetSignboardRotation);
		bIsSignboardRotating = false;
		SetActorTickEnabled(false);
	}
}

void ACSStoreSignboardActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseConfirmWidget();
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnStoreOpenStateChanged.RemoveDynamic(
			this, &ACSStoreSignboardActor::HandleStoreOpenStateChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void ACSStoreSignboardActor::Interact_Implementation(AActor* Interactor)
{
	if (!IsValid(StoreGameState) || !StoreGameState->CanChangeStoreOpenState() ||
		IsValid(ActiveConfirmWidget) || BusinessConfirmWidgetClass == nullptr)
	{
		return;
	}

	const APawn* InteractingPawn = Cast<APawn>(Interactor);
	APlayerController* PlayerController = IsValid(InteractingPawn)
		? Cast<APlayerController>(InteractingPawn->GetController())
		: nullptr;
	if (!IsValid(PlayerController))
	{
		return;
	}

	ActiveConfirmWidget = CreateWidget<UCSStoreBusinessConfirmWidget>(
		PlayerController,
		BusinessConfirmWidgetClass);
	if (!IsValid(ActiveConfirmWidget))
	{
		return;
	}

	ActiveConfirmWidget->SetConfirmationType(!StoreGameState->bIsStoreOpen);
	ActiveConfirmWidget->OnBusinessConfirmed.AddDynamic(
		this, &ACSStoreSignboardActor::HandleBusinessConfirmed);
	ActiveConfirmWidget->OnBusinessCancelled.AddDynamic(
		this, &ACSStoreSignboardActor::HandleBusinessCancelled);
	ActiveConfirmWidget->AddToViewport();

	if (ACSStorePlayerController* StorePlayerController = Cast<ACSStorePlayerController>(PlayerController))
	{
		StorePlayerController->SetGameplayUIInputLock(ActiveConfirmWidget, true);
	}
}

FText ACSStoreSignboardActor::GetInteractionText_Implementation() const
{
	if (!IsValid(StoreGameState))
	{
		return FText::GetEmpty();
	}

	if (StoreGameState->CurrentPhase == ECStorePhase::Settlement ||
		StoreGameState->CurrentPhase == ECStorePhase::Finished)
	{
		return NSLOCTEXT("Store", "SettlementOnly", "정산 시간입니다");
	}

	if (!StoreGameState->CanChangeStoreOpenState())
	{
		return NSLOCTEXT("Store", "StoreNotOpenTime", "영업 시간 전입니다");
	}

	return StoreGameState->bIsStoreOpen
		? NSLOCTEXT("Store", "CloseStore", "영업 종료")
		: NSLOCTEXT("Store", "OpenStore", "영업 시작");
}

void ACSStoreSignboardActor::HandleStoreOpenStateChanged(bool bNewStoreOpen)
{
	// 새벽 02:00 자동 종료처럼 외부에서 상태가 바뀌어도 확인 창을 정리합니다.
	CloseConfirmWidget();
	UpdateSignboardRotation(false);
}

void ACSStoreSignboardActor::HandleBusinessConfirmed(bool bShouldOpen)
{
	if (IsValid(StoreGameState))
	{
		StoreGameState->SetStoreOpen(bShouldOpen);
	}
	CloseConfirmWidget();
}

void ACSStoreSignboardActor::HandleBusinessCancelled()
{
	CloseConfirmWidget();
}

void ACSStoreSignboardActor::CloseConfirmWidget()
{
	if (!IsValid(ActiveConfirmWidget))
	{
		return;
	}

	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(ActiveConfirmWidget->GetOwningPlayer()))
	{
		StorePlayerController->SetGameplayUIInputLock(ActiveConfirmWidget, false);
	}
	ActiveConfirmWidget->RemoveFromParent();
	ActiveConfirmWidget = nullptr;
}

void ACSStoreSignboardActor::UpdateSignboardRotation(bool bSnapToTarget)
{
	if (!IsValid(StoreGameState))
	{
		return;
	}

	// 영업 중에는 Open 면, 그 외에는 Close 면이 보이도록 목표 각도를 정합니다.
	TargetSignboardRotation = StoreGameState->bIsStoreOpen
		? ClosedWorldRotation + FRotator(0.0f, OpenYawOffset, 0.0f)
		: ClosedWorldRotation;

	if (bSnapToTarget)
	{
		SetActorRotation(TargetSignboardRotation);
		bIsSignboardRotating = false;
		return;
	}

	bIsSignboardRotating = true;
	SetActorTickEnabled(true);
}
