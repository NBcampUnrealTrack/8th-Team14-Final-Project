#include "UI/Store/CSDailySummaryWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

void UCSDailySummaryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.AddDynamic(this, &UCSDailySummaryWidget::HandleConfirmClicked);
	RefreshSummary();
}
void UCSDailySummaryWidget::RefreshSummary()
{
	const ACSStoreGameState* State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(State)) return;
	if (IsValid(DailySalesText)) DailySalesText->SetText(FText::Format(NSLOCTEXT("Store", "SummarySales", "오늘 매출: {0}"), FText::AsNumber(State->DailySalesGold)));
	if (IsValid(DailyProfitText)) DailyProfitText->SetText(FText::Format(NSLOCTEXT("Store", "SummaryProfit", "오늘 순이익: {0}"), FText::AsNumber(State->DailyProfitGold)));
	if (IsValid(DailySoldQuantityText)) DailySoldQuantityText->SetText(FText::Format(NSLOCTEXT("Store", "SummaryQuantity", "판매 수량: {0}"), FText::AsNumber(State->DailySoldQuantity)));
}
void UCSDailySummaryWidget::HandleConfirmClicked() { OnDailySummaryConfirmed.Broadcast(); }
