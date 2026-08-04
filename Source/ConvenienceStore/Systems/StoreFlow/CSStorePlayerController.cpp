#include "Systems/StoreFlow/CSStorePlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/HUD/CSStoreHUDWidget.h"

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
	}

	Super::EndPlay(EndPlayReason);
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
	if (StoreGameState == nullptr)
	{
		return;
	}

	const bool bIsClosingUIOpen =
		StoreGameState->CurrentEndOfDayTask != ECEndOfDayTask::None ||
		StoreGameState->CurrentPhase == ECStorePhase::Finished;

	bShowMouseCursor = bIsClosingUIOpen;

	// 단계와 업무 이벤트가 같은 입력 잠금을 여러 번 요청할 수 있으므로 먼저 초기화합니다.
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();

	if (bIsClosingUIOpen)
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
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}
}

void ACSStorePlayerController::ToggleClock()
{
	if (StoreHUDWidget != nullptr)
	{
		StoreHUDWidget->ToggleClockVisibility();
	}
}
