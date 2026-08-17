#include "UI/Store/CSProductAnalysisRowWidget.h"

#include "Components/TextBlock.h"
#include "Components/Widget.h"

void UCSProductAnalysisRowWidget::SetRowData(
	const FText& ProductName,
	int32 SoldQuantity,
	int32 RemainingStock,
	int32 DisposedQuantity)
{
	if (IsValid(ProductNameText)) ProductNameText->SetText(ProductName);
	if (IsValid(SoldQuantityText)) SoldQuantityText->SetText(FText::AsNumber(SoldQuantity));
	if (IsValid(RemainingStockText))
	{
		RemainingStockText->SetText(FText::AsNumber(RemainingStock));
		RemainingStockText->SetColorAndOpacity(
			RemainingStock <= 0 ? OutOfStockTextColor : InStockTextColor);
	}
	if (IsValid(DisposedQuantityText))
	{
		DisposedQuantityText->SetText(FText::AsNumber(DisposedQuantity));
	}

	const bool bOutOfStock = RemainingStock <= 0;
	if (IsValid(DefaultStatusMarker))
	{
		DefaultStatusMarker->SetVisibility(
			bOutOfStock ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (IsValid(OutOfStockStatusBox))
	{
		OutOfStockStatusBox->SetVisibility(
			bOutOfStock ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}
