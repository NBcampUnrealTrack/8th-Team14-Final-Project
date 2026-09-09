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
