#include "Features/Store/CSStoreClosingBellActor.h"
#include "Components/StaticMeshComponent.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Store/CSStoreClosingBellConfirmWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
ACSStoreClosingBellActor::ACSStoreClosingBellActor()
{
	BellMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BellMesh"));
	SetRootComponent(BellMesh);
	BellMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}
void ACSStoreClosingBellActor::BeginPlay() { Super::BeginPlay(); StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>(); }
void ACSStoreClosingBellActor::Interact_Implementation(AActor* Interactor)
{
	if (!IsValid(StoreGameState) || StoreGameState->CurrentPhase != ECStorePhase::ClosingFreeRoam ||
		IsValid(ActiveConfirmWidget) || ConfirmWidgetClass == nullptr) return;
	const APawn* Pawn = Cast<APawn>(Interactor);
	APlayerController* Controller = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Controller)) return;
	ActiveConfirmWidget = CreateWidget<UCSStoreClosingBellConfirmWidget>(Controller, ConfirmWidgetClass);
	if (!IsValid(ActiveConfirmWidget)) return;
	ActiveConfirmWidget->OnClosingBellConfirmed.AddDynamic(this, &ACSStoreClosingBellActor::HandleConfirmed);
	ActiveConfirmWidget->OnClosingBellCancelled.AddDynamic(this, &ACSStoreClosingBellActor::HandleCancelled);
	ActiveConfirmWidget->AddToViewport();
	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Controller)) StoreController->SetGameplayUIInputLock(ActiveConfirmWidget, true);
}
void ACSStoreClosingBellActor::HandleConfirmed()
{
	CloseConfirmWidget();
	if (ACSStoreGameMode* Mode = Cast<ACSStoreGameMode>(GetWorld()->GetAuthGameMode()))
	{
		Mode->StartNextBusinessDay();
	}
}
void ACSStoreClosingBellActor::HandleCancelled() { CloseConfirmWidget(); }
void ACSStoreClosingBellActor::CloseConfirmWidget()
{
	if (!IsValid(ActiveConfirmWidget)) return;
	if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(ActiveConfirmWidget->GetOwningPlayer())) Controller->SetGameplayUIInputLock(ActiveConfirmWidget, false);
	ActiveConfirmWidget->RemoveFromParent();
	ActiveConfirmWidget = nullptr;
}
FText ACSStoreClosingBellActor::GetInteractionText_Implementation() const
{
	if (!IsValid(StoreGameState))
	{
		return FText::GetEmpty();
	}

	if (StoreGameState->CurrentPhase == ECStorePhase::ClosingFreeRoam)
	{
		return NSLOCTEXT("Store", "FinishClosing", "마감 종료");
	}

	// 준비·영업 단계에도 마감 종의 사용 시점을 HUD 안내로 알려드립니다.
	return NSLOCTEXT("Store", "ClosingBellUnavailable", "마감 시간에 사용할 수 있습니다.");
}
