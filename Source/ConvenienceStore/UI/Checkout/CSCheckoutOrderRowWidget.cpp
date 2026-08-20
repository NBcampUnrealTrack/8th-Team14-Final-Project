#include "UI/Checkout/CSCheckoutOrderRowWidget.h"

#include "Components/TextBlock.h"

void UCSCheckoutOrderRowWidget::SetLineItem(const FCSCheckoutLineItem& InLineItem)
{
	// 텍스트를 갱신하기 전에 최초 표시 여부를 보존하여 알맞은 WBP 연출을 선택합니다.
	const bool bIsInitialDisplay = !bHasDisplayedLineItem;

	// 블루프린트에 같은 이름의 TextBlock이 배치된 경우에만 값을 갱신합니다.
	if (IsValid(Text_ProductName))
	{
		Text_ProductName->SetText(InLineItem.ItemName);
	}

	if (IsValid(Text_Quantity))
	{
		Text_Quantity->SetText(FText::AsNumber(InLineItem.Quantity));
	}

	if (IsValid(Text_UnitPrice))
	{
		Text_UnitPrice->SetText(FText::AsNumber(InLineItem.UnitSalePrice));
	}

	if (IsValid(Text_LineTotal))
	{
		Text_LineTotal->SetText(FText::AsNumber(InLineItem.LineTotalSalePrice));
	}

	// WBP가 이벤트를 받는 시점에는 모든 표시 값이 새 데이터로 바뀌어 있어야 합니다.
	bHasDisplayedLineItem = true;
	if (bIsInitialDisplay)
	{
		OnInitialLineItemDisplayed();
	}
	else
	{
		OnLineItemQuantityUpdated();
	}
}
