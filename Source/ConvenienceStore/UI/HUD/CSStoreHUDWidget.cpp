#include "UI/HUD/CSStoreHUDWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

void UCSStoreHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreGameState == nullptr)
	{
		return;
	}

	StoreGameState->OnStorePhaseChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandlePhaseChanged);
	StoreGameState->OnRemainingTimeChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleRemainingTimeChanged);
	StoreGameState->OnGameTimeChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleGameTimeChanged);
	StoreGameState->OnEndOfDayTaskChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleEndOfDayTaskChanged);

	if (CompleteEndOfDayTaskButton != nullptr)
	{
		CompleteEndOfDayTaskButton->OnClicked.AddDynamic(
			this, &UCSStoreHUDWidget::CompleteEndOfDayTask);
	}

	HandlePhaseChanged(StoreGameState->CurrentPhase);
	HandleRemainingTimeChanged(StoreGameState->RemainingSeconds);
	HandleGameTimeChanged(StoreGameState->CurrentGameMinutes);
	HandleEndOfDayTaskChanged(StoreGameState->CurrentEndOfDayTask);

	if (ClockPanel != nullptr)
	{
		ClockPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCSStoreHUDWidget::NativeDestruct()
{
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandlePhaseChanged);
		StoreGameState->OnRemainingTimeChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleRemainingTimeChanged);
		StoreGameState->OnGameTimeChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleGameTimeChanged);
		StoreGameState->OnEndOfDayTaskChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleEndOfDayTaskChanged);
	}

	if (CompleteEndOfDayTaskButton != nullptr)
	{
		CompleteEndOfDayTaskButton->OnClicked.RemoveDynamic(
			this, &UCSStoreHUDWidget::CompleteEndOfDayTask);
	}

	Super::NativeDestruct();
}

void UCSStoreHUDWidget::ToggleClockVisibility()
{
	if (ClockPanel == nullptr)
	{
		return;
	}

	bIsClockVisible = !bIsClockVisible;
	ClockPanel->SetVisibility(bIsClockVisible
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
}

void UCSStoreHUDWidget::HandlePhaseChanged(ECStorePhase NewPhase)
{
	if (PhaseNameText != nullptr && StoreGameState != nullptr)
	{
		PhaseNameText->SetText(StoreGameState->CurrentPhaseDisplayName);
	}

	UpdateEndOfDayPanel();
}

void UCSStoreHUDWidget::HandleRemainingTimeChanged(float NewRemainingSeconds)
{
	if (RemainingTimeText != nullptr)
	{
		RemainingTimeText->SetText(FormatTime(NewRemainingSeconds));
	}
}

void UCSStoreHUDWidget::HandleGameTimeChanged(float NewGameMinutes)
{
	if (ClockText != nullptr)
	{
		ClockText->SetText(FormatGameClock(NewGameMinutes));
	}
}

void UCSStoreHUDWidget::HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask)
{
	UpdateEndOfDayPanel();
}

void UCSStoreHUDWidget::UpdateEndOfDayPanel()
{
	if (StoreGameState == nullptr)
	{
		return;
	}

	const ECEndOfDayTask CurrentTask = StoreGameState->CurrentEndOfDayTask;
	const bool bIsNextDayReady =
		CurrentTask == ECEndOfDayTask::None &&
		StoreGameState->CurrentPhase == ECStorePhase::Finished;
	const bool bHasTask = CurrentTask != ECEndOfDayTask::None;
	const bool bShowPanel = bHasTask || bIsNextDayReady;

	if (EndOfDayPanel != nullptr)
	{
		EndOfDayPanel->SetVisibility(bShowPanel
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}

	if (EndOfDayTaskText != nullptr)
	{
		EndOfDayTaskText->SetText(bIsNextDayReady
			? NSLOCTEXT("Store", "NextDayReady", "02:00 AM — 다음 날을 시작하세요.")
			: GetEndOfDayButtonText(CurrentTask));
	}

	if (CompleteEndOfDayTaskButton != nullptr)
	{
		CompleteEndOfDayTaskButton->SetVisibility(bShowPanel
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}

	if (CompleteEndOfDayTaskButtonText != nullptr)
	{
		CompleteEndOfDayTaskButtonText->SetText(bIsNextDayReady
			? NSLOCTEXT("Store", "StartNextDay", "다음 날 시작")
			: NSLOCTEXT("Store", "CompleteTask", "완료"));
	}
}

void UCSStoreHUDWidget::CompleteEndOfDayTask()
{
	if (ACSStoreGameMode* StoreGameMode =
		Cast<ACSStoreGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (StoreGameState != nullptr &&
			StoreGameState->CurrentEndOfDayTask == ECEndOfDayTask::None &&
			StoreGameState->CurrentPhase == ECStorePhase::Finished)
		{
			StoreGameMode->StartNextBusinessDay();
		}
		else
		{
			StoreGameMode->CompleteCurrentEndOfDayTask();
		}
	}
}

FText UCSStoreHUDWidget::FormatTime(float Seconds) const
{
	const int32 TotalSeconds = FMath::CeilToInt(FMath::Max(0.0f, Seconds));
	const int32 Minutes = TotalSeconds / 60;
	const int32 RemainingSeconds = TotalSeconds % 60;

	return FText::FromString(FString::Printf(
		TEXT("%02d:%02d"), Minutes, RemainingSeconds));
}

FText UCSStoreHUDWidget::FormatGameClock(float GameMinutes) const
{
	const int32 TotalMinutes = FMath::FloorToInt(GameMinutes) % 1440;
	const int32 Hour24 = TotalMinutes / 60;
	const int32 Minutes = TotalMinutes % 60;
	const bool bIsPM = Hour24 >= 12;
	const int32 Hour12 = (Hour24 % 12 == 0) ? 12 : Hour24 % 12;

	return FText::FromString(FString::Printf(
		TEXT("%02d:%02d %s"),
		Hour12,
		Minutes,
		bIsPM ? TEXT("PM") : TEXT("AM")));
}

FText UCSStoreHUDWidget::GetEndOfDayButtonText(ECEndOfDayTask Task) const
{
	switch (Task)
	{
	case ECEndOfDayTask::OrderReview:
		return NSLOCTEXT("Store", "OrderReview", "발주 확인 완료");
	case ECEndOfDayTask::Settlement:
		return NSLOCTEXT("Store", "Settlement", "정산 완료");
	case ECEndOfDayTask::SalesReport:
		return NSLOCTEXT("Store", "SalesReport", "판매 통계 확인");
	case ECEndOfDayTask::ReputationUpdate:
		return NSLOCTEXT("Store", "ReputationUpdate", "평판 변화 확인");
	case ECEndOfDayTask::None:
	default:
		return FText::GetEmpty();
	}
}
