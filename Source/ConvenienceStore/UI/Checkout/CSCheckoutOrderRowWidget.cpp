#include "UI/Checkout/CSCheckoutOrderRowWidget.h"

#include "Components/TextBlock.h"

void UCSCheckoutOrderRowWidget::SetLineItem(const FCSCheckoutLineItem& InLineItem)
{
	// 블루프린트에 같은 이름의 TextBlock이 배치된 경우에만 값을 갱신합니다.
	if (IsValid(ItemNameText))
	{
		ItemNameText->SetText(InLineItem.ItemName);
	}

	if (IsValid(QuantityText))
	{
		QuantityText->SetText(FText::Format(
			NSLOCTEXT("Checkout", "OrderRowQuantity", "수량: {0}"),
			FText::AsNumber(InLineItem.Quantity)));
	}

	if (IsValid(PriceText))
	{
		// 단가와 수량을 함께 표시하여 계산 결과를 플레이어가 확인할 수 있게 합니다.
		PriceText->SetText(FText::Format(
			NSLOCTEXT("Checkout", "OrderRowPrice", "단가: {0} 골드 / 합계: {1} 골드"),
			FText::AsNumber(InLineItem.UnitSalePrice),
			FText::AsNumber(InLineItem.LineTotalSalePrice)));
	}
}
