#include "Features/NoticeBoard/UI/CSNoticeBoardDeliveryTabWidget.h"

#include "Features/NoticeBoard/UI/CSNoticeBoardWidget.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"

void UCSNoticeBoardDeliveryTabWidget::NativeConstruct()
{
	Super::NativeConstruct();

	DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	if (IsValid(DeliveryOrderSubsystem))
	{
		DeliveryOrderSubsystem->OnDeliveryOrdersChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleDeliveryOrdersChanged);
	}
	RefreshDeliveryOrderList();
}

void UCSNoticeBoardDeliveryTabWidget::NativeDestruct()
{
	if (IsValid(DeliveryOrderSubsystem))
	{
		DeliveryOrderSubsystem->OnDeliveryOrdersChanged.RemoveDynamic(
			this,
			&ThisClass::HandleDeliveryOrdersChanged);
	}
	DeliveryOrderSubsystem = nullptr;
	Super::NativeDestruct();
}

void UCSNoticeBoardDeliveryTabWidget::RefreshDeliveryOrderList()
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		RefreshDeliveryOrderListView(TArray<FCSDeliveryOrderRuntime>(), 0, 0);
		RefreshRecentDeliveryResults(TArray<FCSDeliveryOrderRuntime>());
		RefreshDeliveryOrderView(FCSDeliveryOrderRuntime(), false, FText::GetEmpty());
		return;
	}

	FText AvailabilityMessage;
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	const bool bCanAccept = DisplayedOrder.OrderId != INDEX_NONE &&
		DeliveryOrderSubsystem->CanAcceptOrder(DisplayedOrder.OrderId, AvailabilityMessage);
	RefreshDeliveryOrderView(DisplayedOrder, bCanAccept, AvailabilityMessage);
	RefreshDeliveryOrderListView(
		DeliveryOrderSubsystem->GetDeliveryOrders(),
		DeliveryOrderSubsystem->GetTodayGeneratedOrderCount(),
		DeliveryOrderSubsystem->GetAcceptedOrderCount());
	RefreshRecentDeliveryResults(DeliveryOrderSubsystem->GetRecentDeliveryResults());
}

TArray<FCSDeliveryOrderRuntime> UCSNoticeBoardDeliveryTabWidget::GetDeliveryOrders() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetDeliveryOrders()
		: TArray<FCSDeliveryOrderRuntime>();
}

TArray<FCSDeliveryOrderRuntime> UCSNoticeBoardDeliveryTabWidget::GetRecentDeliveryResults() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetRecentDeliveryResults()
		: TArray<FCSDeliveryOrderRuntime>();
}

bool UCSNoticeBoardDeliveryTabWidget::CanAcceptOrder(const int32 OrderId, FText& OutReason) const
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutReason = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystem", "배달 주문 시스템을 찾을 수 없습니다.");
		return false;
	}
	return DeliveryOrderSubsystem->CanAcceptOrder(OrderId, OutReason);
}

bool UCSNoticeBoardDeliveryTabWidget::AcceptOrder(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemAccept", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->AcceptOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	// 서브시스템이 변경 알림을 방송하지만, 실패한 요청도 버튼 상태를 즉시 재평가합니다.
	RefreshDeliveryOrderList();
	return bSucceeded;
}

bool UCSNoticeBoardDeliveryTabWidget::DeclineOrder(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemDecline", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->DeclineOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	RefreshDeliveryOrderList();
	return bSucceeded;
}

bool UCSNoticeBoardDeliveryTabWidget::CancelOrder(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemCancel", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->CancelOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	RefreshDeliveryOrderList();
	return bSucceeded;
}

bool UCSNoticeBoardDeliveryTabWidget::PrepareOrderForDispatch(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemDispatch", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->SetActiveDispatchOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	RefreshDeliveryOrderList();
	return bSucceeded;
}

FCSDeliveryOrderRuntime UCSNoticeBoardDeliveryTabWidget::GetDisplayedOrder() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetCurrentOrder()
		: FCSDeliveryOrderRuntime();
}

int32 UCSNoticeBoardDeliveryTabWidget::GetTodayGeneratedOrderCount() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetTodayGeneratedOrderCount()
		: 0;
}

int32 UCSNoticeBoardDeliveryTabWidget::GetAcceptedOrderCount() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetAcceptedOrderCount()
		: 0;
}

bool UCSNoticeBoardDeliveryTabWidget::AcceptDisplayedOrder(FText& OutMessage)
{
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	if (DisplayedOrder.OrderId == INDEX_NONE)
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDisplayedDeliveryOrder", "수락할 배달 주문이 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}
	return AcceptOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSNoticeBoardDeliveryTabWidget::DeclineDisplayedOrder(FText& OutMessage)
{
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	if (DisplayedOrder.OrderId == INDEX_NONE)
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDisplayedDeliveryOrderDecline", "거절할 배달 주문이 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}
	return DeclineOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSNoticeBoardDeliveryTabWidget::CancelActiveOrder(FText& OutMessage)
{
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	if (DisplayedOrder.OrderId == INDEX_NONE)
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDisplayedDeliveryOrderCancel", "포기할 배달 주문이 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}
	return CancelOrder(DisplayedOrder.OrderId, OutMessage);
}

void UCSNoticeBoardDeliveryTabWidget::CloseOrderWindow()
{
	if (UCSNoticeBoardWidget* NoticeBoardWidget = GetTypedOuter<UCSNoticeBoardWidget>())
	{
		NoticeBoardWidget->CloseNoticeBoardWindow();
	}
}

void UCSNoticeBoardDeliveryTabWidget::HandleDeliveryOrdersChanged()
{
	RefreshDeliveryOrderList();
}
