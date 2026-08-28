#include "Features/UI/CSDeliveryOrderEntryWidget.h"

#include "Features/Delivery/CSDeliveryOrderTerminalActor.h"

void UCSDeliveryOrderEntryWidget::InitializeDeliveryOrderEntry(
	ACSDeliveryOrderTerminalActor* InOrderTerminal,
	const FCSDeliveryOrderRuntime& InOrder)
{
	OrderTerminal = InOrderTerminal;
	DisplayedOrder = InOrder;

	FText AvailabilityMessage;
	const bool bCanAccept = CanAcceptDisplayedOrder(AvailabilityMessage);
	RefreshDeliveryOrderEntryView(DisplayedOrder, bCanAccept, AvailabilityMessage);
}

bool UCSDeliveryOrderEntryWidget::CanAcceptDisplayedOrder(FText& OutReason) const
{
	if (!IsValid(OrderTerminal))
	{
		OutReason = NSLOCTEXT("Delivery", "NoOrderTerminal", "배달 주문 단말기를 찾을 수 없습니다.");
		return false;
	}

	if (DisplayedOrder.Status != ECSDeliveryOrderStatus::PendingAcceptance)
	{
		OutReason = FText::GetEmpty();
		return false;
	}
	return OrderTerminal->CanAcceptDeliveryOrder(DisplayedOrder.OrderId, OutReason);
}

bool UCSDeliveryOrderEntryWidget::AcceptDisplayedOrder(FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->AcceptDeliveryOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSDeliveryOrderEntryWidget::DeclineDisplayedOrder(FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->DeclineDeliveryOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSDeliveryOrderEntryWidget::CancelDisplayedOrder(FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->CancelDeliveryOrder(DisplayedOrder.OrderId, OutMessage);
}
