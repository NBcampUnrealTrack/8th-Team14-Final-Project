#include "UI/Store/CSDailySummaryWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/Store/CSProductAnalysisWidget.h"

void UCSDailySummaryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 기존 AddDynamic 직접 연결은 위젯 재구성 시 중복될 수 있어 수명 주기와 함께 관리합니다.
	// if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.AddDynamic(this, &UCSDailySummaryWidget::HandleConfirmClicked);
	if (IsValid(ConfirmButton))
	{
		ConfirmButton->OnClicked.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleConfirmClicked);
	}
	if (IsValid(ProductAnalysisButton))
	{
		ProductAnalysisButton->OnClicked.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleProductAnalysisClicked);
	}

	StoreGameState = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnDailySalesChanged.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleDailySummaryDataChanged);
		StoreGameState->OnReputationChanged.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleReputationChanged);
	}
	RefreshSummary();
}

void UCSDailySummaryWidget::NativeDestruct()
{
	if (IsValid(ConfirmButton))
	{
		ConfirmButton->OnClicked.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleConfirmClicked);
	}
	if (IsValid(ProductAnalysisButton))
	{
		ProductAnalysisButton->OnClicked.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleProductAnalysisClicked);
	}
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnDailySalesChanged.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleDailySummaryDataChanged);
		StoreGameState->OnReputationChanged.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleReputationChanged);
	}
	StoreGameState = nullptr;

	Super::NativeDestruct();
}

void UCSDailySummaryWidget::RefreshSummary()
{
	const ACSStoreGameState* State = IsValid(StoreGameState)
		? StoreGameState.Get()
		: GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(State)) return;

	const FText SalesText = FText::AsNumber(State->DailySalesGold);
	const FText WasteLossText = FText::AsNumber(State->DailyWasteLossGold);
	const FText RentText = FText::AsNumber(State->GetDailyRentGold());
	const FText NextRentText = FText::AsNumber(State->GetNextRentGold());
	const FText RentDDayDisplayText = FText::Format(
		NSLOCTEXT("StoreSummary", "RentDDay", "D - {0}"),
		FText::AsNumber(State->GetDaysUntilNextRentPayment()));
	const FText TotalLossText = FText::AsNumber(State->GetDailyTotalLossGold());
	const FText NetProfitText = FText::AsNumber(State->GetDailyNetProfitGold());

	if (IsValid(BusinessDayResultText))
	{
		BusinessDayResultText->SetText(FText::Format(
			NSLOCTEXT("StoreSummary", "BusinessDayResult", "{0}일차 결과"),
			FText::AsNumber(State->CurrentBusinessDay)));
	}
	if (IsValid(MainNetProfitText)) MainNetProfitText->SetText(NetProfitText);
	if (IsValid(BreakdownSalesText)) BreakdownSalesText->SetText(SalesText);
	if (IsValid(BreakdownTotalLossText)) BreakdownTotalLossText->SetText(TotalLossText);
	if (IsValid(BreakdownNetProfitText)) BreakdownNetProfitText->SetText(NetProfitText);
	if (IsValid(DailySalesText)) DailySalesText->SetText(SalesText);
	if (IsValid(DailyWasteLossText)) DailyWasteLossText->SetText(WasteLossText);
	if (IsValid(DailyRentText)) DailyRentText->SetText(RentText);
	if (IsValid(RentAmountText)) RentAmountText->SetText(NextRentText);
	if (IsValid(RentDDayText)) RentDDayText->SetText(RentDDayDisplayText);
	if (IsValid(DailySoldQuantityText))
	{
		DailySoldQuantityText->SetText(FText::Format(
			NSLOCTEXT("StoreSummary", "SoldQuantity", "{0}건"),
			FText::AsNumber(State->DailySoldQuantity)));
	}
	if (IsValid(ReputationScoreText))
	{
		ReputationScoreText->SetText(FText::AsNumber(State->GetReputationScore()));
	}

	ApplyDeltaPresentation(
		State->GetDailyNetProfitDeltaGold(),
		NetProfitDeltaText,
		NetProfitDeltaIcon,
		NSLOCTEXT("StoreSummary", "ProfitNonNegative", "전일 대비 {0}G 증가"),
		NSLOCTEXT("StoreSummary", "ProfitNegative", "전일 대비 {0}G 감소"));
	ApplyDeltaPresentation(
		State->GetDailyReputationDelta(),
		ReputationDeltaText,
		ReputationDeltaIcon,
		NSLOCTEXT("StoreSummary", "ReputationNonNegative", "{0}"),
		NSLOCTEXT("StoreSummary", "ReputationNegative", "{0}"));
}

void UCSDailySummaryWidget::HandleDailySummaryDataChanged()
{
	RefreshSummary();
}

void UCSDailySummaryWidget::HandleReputationChanged(int32 NewReputationScore)
{
	(void)NewReputationScore;
	RefreshSummary();
}

void UCSDailySummaryWidget::ApplyDeltaPresentation(
	int32 Delta,
	UTextBlock* DeltaText,
	UImage* DeltaIcon,
	const FText& NonNegativeFormat,
	const FText& NegativeFormat)
{
	const bool bIsNegative = Delta < 0;
	const FLinearColor DisplayColor = bIsNegative ? NegativeColor : NonNegativeColor;
	const int64 AbsoluteDelta = FMath::Abs(static_cast<int64>(Delta));

	if (IsValid(DeltaText))
	{
		DeltaText->SetText(FText::Format(
			bIsNegative ? NegativeFormat : NonNegativeFormat,
			FText::AsNumber(AbsoluteDelta)));
		DeltaText->SetColorAndOpacity(DisplayColor);
	}
	if (IsValid(DeltaIcon))
	{
		DeltaIcon->SetColorAndOpacity(DisplayColor);
		DeltaIcon->SetRenderTransformAngle(bIsNegative ? 180.0f : 0.0f);
	}
}

void UCSDailySummaryWidget::HandleConfirmClicked() { OnDailySummaryConfirmed.Broadcast(); }

void UCSDailySummaryWidget::HandleProductAnalysisClicked()
{
	if (IsValid(ProductAnalysisPanel))
	{
		ProductAnalysisPanel->OpenAnalysis();
	}
}
