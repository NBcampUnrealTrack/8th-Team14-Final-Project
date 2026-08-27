#include "UI/Store/CSDailySummaryWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
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
	if (IsValid(TotalExpenseHoverButton))
	{
		TotalExpenseHoverButton->OnHovered.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleTotalExpenseHovered);
		TotalExpenseHoverButton->OnUnhovered.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleTotalExpenseUnhovered);
	}
	if (IsValid(SeparateLossHoverButton))
	{
		SeparateLossHoverButton->OnHovered.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleSeparateLossHovered);
		SeparateLossHoverButton->OnUnhovered.AddUniqueDynamic(
			this, &UCSDailySummaryWidget::HandleSeparateLossUnhovered);
	}
	if (IsValid(ExpenseBreakdownPopup)) ExpenseBreakdownPopup->SetVisibility(ESlateVisibility::Collapsed);
	if (IsValid(SeparateLossBreakdownPopup)) SeparateLossBreakdownPopup->SetVisibility(ESlateVisibility::Collapsed);

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
	if (IsValid(TotalExpenseHoverButton))
	{
		TotalExpenseHoverButton->OnHovered.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleTotalExpenseHovered);
		TotalExpenseHoverButton->OnUnhovered.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleTotalExpenseUnhovered);
	}
	if (IsValid(SeparateLossHoverButton))
	{
		SeparateLossHoverButton->OnHovered.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleSeparateLossHovered);
		SeparateLossHoverButton->OnUnhovered.RemoveDynamic(
			this, &UCSDailySummaryWidget::HandleSeparateLossUnhovered);
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
	const FText TotalLossText = FText::AsNumber(State->GetDailyTotalExpenseGold());
	const FText SeparateLossText = FText::AsNumber(State->GetDailySeparateLossGold());
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
	if (IsValid(DailyTotalExpenseText)) DailyTotalExpenseText->SetText(TotalLossText);
	if (IsValid(DailySeparateLossText)) DailySeparateLossText->SetText(SeparateLossText);
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

	const bool bHasProductOrderExpense = State->DailyProductOrderExpenseGold > 0;
	const bool bHasFacilityOrderExpense = State->DailyFacilityOrderExpenseGold > 0;
	const bool bHasQuickProductExpense = State->DailyQuickProductOrderExpenseGold > 0;
	const bool bHasQuickFacilityExpense = State->DailyQuickFacilityOrderExpenseGold > 0;
	const bool bHasRentExpense = State->DailyRentGold > 0;
	const bool bHasAnyExpense = bHasProductOrderExpense || bHasFacilityOrderExpense ||
		bHasQuickProductExpense || bHasQuickFacilityExpense || bHasRentExpense;

	if (IsValid(ProductOrderExpenseRow)) ProductOrderExpenseRow->SetVisibility(bHasProductOrderExpense ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(FacilityOrderExpenseRow)) FacilityOrderExpenseRow->SetVisibility(bHasFacilityOrderExpense ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(QuickProductOrderExpenseRow)) QuickProductOrderExpenseRow->SetVisibility(bHasQuickProductExpense ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(QuickFacilityOrderExpenseRow)) QuickFacilityOrderExpenseRow->SetVisibility(bHasQuickFacilityExpense ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(RentExpenseRow)) RentExpenseRow->SetVisibility(bHasRentExpense ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(ProductOrderExpenseText)) ProductOrderExpenseText->SetText(FText::AsNumber(State->DailyProductOrderExpenseGold));
	if (IsValid(FacilityOrderExpenseText)) FacilityOrderExpenseText->SetText(FText::AsNumber(State->DailyFacilityOrderExpenseGold));
	if (IsValid(QuickProductOrderExpenseText)) QuickProductOrderExpenseText->SetText(FText::AsNumber(State->DailyQuickProductOrderExpenseGold));
	if (IsValid(QuickFacilityOrderExpenseText)) QuickFacilityOrderExpenseText->SetText(FText::AsNumber(State->DailyQuickFacilityOrderExpenseGold));
	if (IsValid(RentExpenseText)) RentExpenseText->SetText(RentText);
	if (IsValid(ExpenseDivider)) ExpenseDivider->SetVisibility(bHasAnyExpense ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(ExpenseTotalRow)) ExpenseTotalRow->SetVisibility(bHasAnyExpense ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(ExpenseTotalText)) ExpenseTotalText->SetText(TotalLossText);
	if (IsValid(NoExpenseText)) NoExpenseText->SetVisibility(bHasAnyExpense ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	const bool bHasWasteLoss = State->DailyWasteLossGold > 0;
	const bool bHasShopliftLoss = State->DailyShopliftLossGold > 0;
	const bool bHasAnySeparateLoss = bHasWasteLoss || bHasShopliftLoss;
	if (IsValid(WasteLossRow)) WasteLossRow->SetVisibility(bHasWasteLoss ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(ShopliftLossRow)) ShopliftLossRow->SetVisibility(bHasShopliftLoss ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(WasteLossAmountText)) WasteLossAmountText->SetText(WasteLossText);
	if (IsValid(ShopliftLossAmountText)) ShopliftLossAmountText->SetText(FText::AsNumber(State->DailyShopliftLossGold));
	if (IsValid(SeparateLossDivider)) SeparateLossDivider->SetVisibility(bHasAnySeparateLoss ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(SeparateLossTotalRow)) SeparateLossTotalRow->SetVisibility(bHasAnySeparateLoss ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(SeparateLossTotalText)) SeparateLossTotalText->SetText(SeparateLossText);
	if (IsValid(NoSeparateLossText)) NoSeparateLossText->SetVisibility(bHasAnySeparateLoss ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

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

void UCSDailySummaryWidget::HandleTotalExpenseHovered()
{
	if (IsValid(ExpenseBreakdownPopup))
	{
		ExpenseBreakdownPopup->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UCSDailySummaryWidget::HandleTotalExpenseUnhovered()
{
	if (IsValid(ExpenseBreakdownPopup))
	{
		ExpenseBreakdownPopup->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCSDailySummaryWidget::HandleSeparateLossHovered()
{
	if (IsValid(SeparateLossBreakdownPopup))
	{
		SeparateLossBreakdownPopup->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UCSDailySummaryWidget::HandleSeparateLossUnhovered()
{
	if (IsValid(SeparateLossBreakdownPopup))
	{
		SeparateLossBreakdownPopup->SetVisibility(ESlateVisibility::Collapsed);
	}
}
