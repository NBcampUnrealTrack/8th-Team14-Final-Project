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
	StoreGameState->OnStoreOpenStateChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleStoreOpenStateChanged);
	StoreGameState->OnRemainingTimeChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleRemainingTimeChanged);
	StoreGameState->OnGameTimeChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleGameTimeChanged);
	StoreGameState->OnEndOfDayTaskChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleEndOfDayTaskChanged);
	StoreGameState->OnDailySalesChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleDailySalesChanged);

	if (CompleteEndOfDayTaskButton != nullptr)
	{
		CompleteEndOfDayTaskButton->OnClicked.AddDynamic(
			this, &UCSStoreHUDWidget::CompleteEndOfDayTask);
	}

	HandlePhaseChanged(StoreGameState->CurrentPhase);
	HandleRemainingTimeChanged(StoreGameState->RemainingSeconds);
	HandleGameTimeChanged(StoreGameState->CurrentGameMinutes);
	HandleEndOfDayTaskChanged(StoreGameState->CurrentEndOfDayTask);
	UpdateDailySalesDisplay();

	if (ClockPanel != nullptr)
	{
		ClockPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// HUD가 생성될 때 이전 플레이 세션의 조준 안내가 보이지 않도록 초기화합니다.
	ClearInteractionContext();
}

void UCSStoreHUDWidget::NativeDestruct()
{
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandlePhaseChanged);
		StoreGameState->OnStoreOpenStateChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleStoreOpenStateChanged);
		StoreGameState->OnRemainingTimeChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleRemainingTimeChanged);
		StoreGameState->OnGameTimeChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleGameTimeChanged);
		StoreGameState->OnEndOfDayTaskChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleEndOfDayTaskChanged);
		StoreGameState->OnDailySalesChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleDailySalesChanged);
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

void UCSStoreHUDWidget::SetInteractionPrompt(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (!IsValid(InteractionPromptText))
	{
		return;
	}

	if (Message.IsEmpty())
	{
		InteractionPromptText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	InteractionPromptText->SetText(Message);
	InteractionPromptText->SetColorAndOpacity(FSlateColor(TextColor));
	InteractionPromptText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCSStoreHUDWidget::ShowPlacementUnavailablePrompt()
{
	// 빈 슬롯이 없을 때는 상품 상세 정보가 판단을 방해하지 않도록 함께 숨깁니다.
	if (IsValid(ProductPriceText))
	{
		ProductPriceText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IsValid(ProductShelfLifeText))
	{
		ProductShelfLifeText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IsValid(StorageWarningText))
	{
		StorageWarningText->SetVisibility(ESlateVisibility::Collapsed);
	}

	SetInteractionPrompt(
		NSLOCTEXT("Interaction", "ShelfFull", "진열할 빈 슬롯이 없습니다."),
		FLinearColor(1.0f, 0.25f, 0.25f, 1.0f));
}

void UCSStoreHUDWidget::SetProductPrice(const FText& Message)
{
	if (!IsValid(ProductPriceText))
	{
		return;
	}

	if (Message.IsEmpty())
	{
		ProductPriceText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ProductPriceText->SetText(Message);
	ProductPriceText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.35f, 1.0f)));
	ProductPriceText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCSStoreHUDWidget::SetProductShelfLife(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (!IsValid(ProductShelfLifeText))
	{
		return;
	}

	if (Message.IsEmpty())
	{
		ProductShelfLifeText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ProductShelfLifeText->SetText(Message);
	ProductShelfLifeText->SetColorAndOpacity(FSlateColor(TextColor));
	ProductShelfLifeText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCSStoreHUDWidget::SetStorageWarning(const FText& Message)
{
	if (!IsValid(StorageWarningText))
	{
		return;
	}

	if (Message.IsEmpty())
	{
		StorageWarningText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	StorageWarningText->SetText(Message);
	StorageWarningText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.1f, 1.0f)));
	StorageWarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCSStoreHUDWidget::ClearInteractionContext()
{
	if (IsValid(InteractionPromptText))
	{
		InteractionPromptText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IsValid(ProductPriceText))
	{
		ProductPriceText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IsValid(ProductShelfLifeText))
	{
		ProductShelfLifeText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IsValid(StorageWarningText))
	{
		StorageWarningText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCSStoreHUDWidget::HandlePhaseChanged(ECStorePhase NewPhase)
{
	UpdatePhaseNameDisplay();

	if (NewPhase == ECStorePhase::Settlement || NewPhase == ECStorePhase::Finished)
	{
		// 조준점이 숨겨지는 단계에서는 이전 배치 안내도 함께 숨깁니다.
		ClearInteractionContext();
	}

	UpdateEndOfDayPanel();
}

void UCSStoreHUDWidget::HandleStoreOpenStateChanged(bool bNewStoreOpen)
{
	(void)bNewStoreOpen;

	// 영업 단계에서 입간판으로 상태를 바꾸면 HUD 문구도 즉시 함께 갱신합니다.
	UpdatePhaseNameDisplay();
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

void UCSStoreHUDWidget::HandleDailySalesChanged()
{
	UpdateDailySalesDisplay();
}

void UCSStoreHUDWidget::UpdatePhaseNameDisplay()
{
	if (!IsValid(PhaseNameText) || !IsValid(StoreGameState))
	{
		return;
	}

	// 영업 가능 시간에는 단계명이 아닌 실제 영업 상태를 우선 표시합니다.
	if (StoreGameState->CurrentPhase == ECStorePhase::Open)
	{
		if (StoreGameState->bIsStoreOpen)
		{
			PhaseNameText->SetText(NSLOCTEXT("Store", "BusinessOpen", "영업중"));
		}
		else
		{
			// 오늘 영업을 시작하지 않았다면 종료가 아닌 개점 대기 상태로 안내합니다.
			PhaseNameText->SetText(StoreGameState->bHasStoreOpenedToday
				? NSLOCTEXT("Store", "BusinessClosed", "영업 종료")
				: NSLOCTEXT("Store", "AwaitingOpening", "개점 대기"));
		}
		return;
	}

	PhaseNameText->SetText(StoreGameState->CurrentPhaseDisplayName);
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

void UCSStoreHUDWidget::UpdateDailySalesDisplay()
{
	if (StoreGameState == nullptr)
	{
		return;
	}

	// HUD 블루프린트에 해당 텍스트가 배치된 경우에만 오늘의 판매 요약을 표시합니다.
	if (DailySalesText != nullptr)
	{
		DailySalesText->SetText(FText::Format(
			NSLOCTEXT("Store", "DailySales", "오늘 매출: {0} 골드"),
			FText::AsNumber(StoreGameState->DailySalesGold)));
	}

	if (DailyProfitText != nullptr)
	{
		DailyProfitText->SetText(FText::Format(
			NSLOCTEXT("Store", "DailyProfit", "오늘 이익: {0} 골드"),
			FText::AsNumber(StoreGameState->DailyProfitGold)));
	}

	if (DailySoldQuantityText != nullptr)
	{
		DailySoldQuantityText->SetText(FText::Format(
			NSLOCTEXT("Store", "DailySoldQuantity", "판매 수량: {0}"),
			FText::AsNumber(StoreGameState->DailySoldQuantity)));
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
