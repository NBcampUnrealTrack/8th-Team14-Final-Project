#include "Features/NoticeBoard/UI/CSNoticeBoardDeliveryOrderEntryWidget.h"

#include "Features/NoticeBoard/UI/CSNoticeBoardDeliveryTabWidget.h"

void UCSNoticeBoardDeliveryOrderEntryWidget::InitializeDeliveryOrderEntry(
	UCSNoticeBoardDeliveryTabWidget* InDeliveryTab,
	const FCSDeliveryOrderRuntime& InOrder)
{
	DeliveryTab = InDeliveryTab;
	DisplayedOrder = InOrder;

	FText AvailabilityMessage;
	const bool bCanAccept = CanAcceptDisplayedOrder(AvailabilityMessage);
	RefreshDeliveryOrderEntryView(DisplayedOrder, bCanAccept, AvailabilityMessage);
	// 기존 WBP의 클릭 그래프는 호환성을 위해 남기되 수동 선택 버튼은 표시하지 않습니다.
	if (UWidget* PrepareButton = GetWidgetFromName(TEXT("Button_PrepareDispatch")))
	{
		PrepareButton->RemoveFromParent();
	}
}

bool UCSNoticeBoardDeliveryOrderEntryWidget::CanAcceptDisplayedOrder(FText& OutReason) const
{
	if (!IsValid(DeliveryTab))
	{
		OutReason = NSLOCTEXT("NoticeBoard", "NoDeliveryTab", "배달 주문 탭을 찾을 수 없습니다.");
		return false;
	}

	if (DisplayedOrder.Status != ECSDeliveryOrderStatus::PendingAcceptance)
	{
		OutReason = FText::GetEmpty();
		return false;
	}
	return DeliveryTab->CanAcceptOrder(DisplayedOrder.OrderId, OutReason);
}

bool UCSNoticeBoardDeliveryOrderEntryWidget::AcceptDisplayedOrder(FText& OutMessage)
{
	return IsValid(DeliveryTab) && DeliveryTab->AcceptOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSNoticeBoardDeliveryOrderEntryWidget::DeclineDisplayedOrder(FText& OutMessage)
{
	return IsValid(DeliveryTab) && DeliveryTab->DeclineOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSNoticeBoardDeliveryOrderEntryWidget::CancelDisplayedOrder(FText& OutMessage)
{
	return IsValid(DeliveryTab) && DeliveryTab->CancelOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSNoticeBoardDeliveryOrderEntryWidget::PrepareDisplayedOrderForDispatch(FText& OutMessage)
{
	return IsValid(DeliveryTab) && DeliveryTab->PrepareOrderForDispatch(DisplayedOrder.OrderId, OutMessage);
}
