#include "Systems/StoreFlow/CSStorePlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/HUD/CSStoreHUDWidget.h"
#include "UI/Store/CSDailySummaryWidget.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"

void ACSStorePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController() && StoreHUDWidgetClass != nullptr)
	{
		StoreHUDWidget = CreateWidget<UCSStoreHUDWidget>(this, StoreHUDWidgetClass);
		if (StoreHUDWidget != nullptr)
		{
			StoreHUDWidget->AddToPlayerScreen();
		}
	}

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnEndOfDayTaskChanged.AddDynamic(
			this, &ACSStorePlayerController::HandleEndOfDayTaskChanged);
		StoreGameState->OnStorePhaseChanged.AddDynamic(
			this, &ACSStorePlayerController::HandleStorePhaseChanged);
		StoreGameState->OnDailySummaryRequested.AddDynamic(this, &ACSStorePlayerController::HandleDailySummaryRequested);
		UpdateClosingInputMode();
	}
}

void ACSStorePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnEndOfDayTaskChanged.RemoveDynamic(
			this, &ACSStorePlayerController::HandleEndOfDayTaskChanged);
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &ACSStorePlayerController::HandleStorePhaseChanged);
		StoreGameState->OnDailySummaryRequested.RemoveDynamic(this, &ACSStorePlayerController::HandleDailySummaryRequested);
	}

	Super::EndPlay(EndPlayReason);
}

void ACSStorePlayerController::HandleDailySummaryRequested()
{
	if (!IsLocalPlayerController() || IsValid(ActiveDailySummaryWidget) || DailySummaryWidgetClass == nullptr) return;
	ActiveDailySummaryWidget = CreateWidget<UCSDailySummaryWidget>(this, DailySummaryWidgetClass);
	if (!IsValid(ActiveDailySummaryWidget)) return;
	ActiveDailySummaryWidget->OnDailySummaryConfirmed.AddDynamic(this, &ACSStorePlayerController::HandleDailySummaryConfirmed);
	ActiveDailySummaryWidget->AddToViewport();
	SetGameplayUIInputLock(ActiveDailySummaryWidget, true);
}

void ACSStorePlayerController::HandleDailySummaryConfirmed()
{
	if (IsValid(ActiveDailySummaryWidget))
	{
		SetGameplayUIInputLock(ActiveDailySummaryWidget, false);
		ActiveDailySummaryWidget->RemoveFromParent();
		ActiveDailySummaryWidget = nullptr;
	}
	if (ACSStoreGameMode* Mode = Cast<ACSStoreGameMode>(GetWorld()->GetAuthGameMode())) Mode->CompleteDailySummary();
}

void ACSStorePlayerController::HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask)
{
	UpdateClosingInputMode();
}

void ACSStorePlayerController::HandleStorePhaseChanged(ECStorePhase NewPhase)
{
	UpdateClosingInputMode();
}

void ACSStorePlayerController::UpdateClosingInputMode()
{
	const bool bIsClosingUIOpen = IsValid(StoreGameState) &&
		(StoreGameState->CurrentEndOfDayTask != ECEndOfDayTask::None ||
			StoreGameState->CurrentPhase == ECStorePhase::Finished);
	const bool bHasGameplayUIInputLock = HasGameplayUIInputLock();
	const bool bShouldUseUIInput = bIsClosingUIOpen || bHasGameplayUIInputLock;

	bShowMouseCursor = bShouldUseUIInput;

	// 단계와 업무 이벤트가 같은 입력 잠금을 여러 번 요청할 수 있으므로 먼저 초기화합니다.
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();

	if (bShouldUseUIInput)
	{
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
	}

	if (bIsClosingUIOpen)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

		if (StoreHUDWidget != nullptr)
		{
			InputMode.SetWidgetToFocus(StoreHUDWidget->TakeWidget());
		}

		SetInputMode(InputMode);
	}
	else if (bHasGameplayUIInputLock)
	{
		// 발주·계산 UI가 열린 동안에는 단계가 변경되어도 캐릭터 조작으로 돌아가지 않게 UI 입력만 받습니다.
		FInputModeUIOnly InputMode;
		if (UUserWidget* FocusedWidget = GetFocusedGameplayUIWidget())
		{
			InputMode.SetWidgetToFocus(FocusedWidget->TakeWidget());
		}

		SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}
}

void ACSStorePlayerController::SetGameplayUIInputLock(
	UUserWidget* RequestingWidget,
	bool bShouldLock)
{
	if (!IsValid(RequestingWidget))
	{
		return;
	}

	if (bShouldLock)
	{
		// 같은 위젯이 여러 번 열림을 알리더라도 입력 잠금 목록에는 한 번만 등록합니다.
		GameplayUIInputLockWidgets.AddUnique(RequestingWidget);
	}
	else
	{
		GameplayUIInputLockWidgets.Remove(RequestingWidget);
	}

	// UI가 열리거나 닫히는 즉시 커서, 이동 제한, 포커스를 같은 규칙으로 갱신합니다.
	UpdateClosingInputMode();

	// 이 함수를 거쳐가는 모든 UI 열림/닫힘을 방송
	OnGameplayUIInputLockChanged.Broadcast(RequestingWidget, bShouldLock);
}

bool ACSStorePlayerController::HasGameplayUIInputLock() const
{
	return GameplayUIInputLockWidgets.ContainsByPredicate(
		[](const TObjectPtr<UUserWidget>& Widget)
		{
			return IsValid(Widget);
		});
}

UUserWidget* ACSStorePlayerController::GetFocusedGameplayUIWidget() const
{
	for (int32 WidgetIndex = GameplayUIInputLockWidgets.Num() - 1;
		WidgetIndex >= 0;
		--WidgetIndex)
	{
		if (IsValid(GameplayUIInputLockWidgets[WidgetIndex]))
		{
			return GameplayUIInputLockWidgets[WidgetIndex];
		}
	}

	return nullptr;
}

void ACSStorePlayerController::ShowPlacementUnavailablePrompt()
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->ShowPlacementUnavailablePrompt();
	}
}

void ACSStorePlayerController::SetInteractionPrompt(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetInteractionPrompt(Message, TextColor);
	}
}

void ACSStorePlayerController::SetProductPrice(const FText& Message)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetProductPrice(Message);
	}
}

void ACSStorePlayerController::SetProductShelfLife(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetProductShelfLife(Message, TextColor);
	}
}

void ACSStorePlayerController::SetProductFreshness(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetProductFreshness(Message, TextColor);
	}
}

void ACSStorePlayerController::SetStorageWarning(const FText& Message)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetStorageWarning(Message);
	}
}

void ACSStorePlayerController::ClearInteractionContext()
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->ClearInteractionContext();
	}
}
