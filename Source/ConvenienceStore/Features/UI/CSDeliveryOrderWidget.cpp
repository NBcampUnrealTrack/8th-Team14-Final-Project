#include "Features/UI/CSDeliveryOrderWidget.h"

#include "Features/Delivery/CSDeliveryOrderTerminalActor.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"

void UCSDeliveryOrderWidget::InitializeDeliveryOrderTerminal(
	ACSDeliveryOrderTerminalActor* InOrderTerminal)
{
	OrderTerminal = InOrderTerminal;
	if (!IsValid(OrderTerminal))
	{
		return;
	}

	BoundDeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	if (IsValid(BoundDeliveryOrderSubsystem))
	{
		BoundDeliveryOrderSubsystem->OnDeliveryOrdersChanged.AddDynamic(
			this,
			&UCSDeliveryOrderWidget::HandleDeliveryOrdersChanged);
	}
	RefreshOrderList();
}

void UCSDeliveryOrderWidget::RefreshOrderList()
{
	if (!IsValid(OrderTerminal))
	{
		return;
	}

	FText AvailabilityMessage;
	const bool bCanAccept = OrderTerminal->CanAcceptActiveDeliveryOrder(AvailabilityMessage);
	RefreshDeliveryOrderView(
		OrderTerminal->GetActiveDeliveryOrder(),
		bCanAccept,
		AvailabilityMessage);
	RefreshDeliveryOrderListView(
		OrderTerminal->GetDeliveryOrders(),
		OrderTerminal->GetTodayGeneratedOrderCount(),
		OrderTerminal->GetAcceptedOrderCount());
}

FCSDeliveryOrderRuntime UCSDeliveryOrderWidget::GetDisplayedOrder() const
{
	return IsValid(OrderTerminal)
		? OrderTerminal->GetActiveDeliveryOrder()
		: FCSDeliveryOrderRuntime();
}

TArray<FCSDeliveryOrderRuntime> UCSDeliveryOrderWidget::GetDisplayedOrders() const
{
	return IsValid(OrderTerminal)
		? OrderTerminal->GetDeliveryOrders()
		: TArray<FCSDeliveryOrderRuntime>();
}

int32 UCSDeliveryOrderWidget::GetTodayGeneratedOrderCount() const
{
	return IsValid(OrderTerminal) ? OrderTerminal->GetTodayGeneratedOrderCount() : 0;
}

int32 UCSDeliveryOrderWidget::GetAcceptedOrderCount() const
{
	return IsValid(OrderTerminal) ? OrderTerminal->GetAcceptedOrderCount() : 0;
}

bool UCSDeliveryOrderWidget::AcceptDisplayedOrder(FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->AcceptActiveDeliveryOrder(OutMessage);
}

bool UCSDeliveryOrderWidget::DeclineDisplayedOrder(FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->DeclineActiveDeliveryOrder(OutMessage);
}

bool UCSDeliveryOrderWidget::CancelActiveOrder(FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->CancelActiveDeliveryOrder(OutMessage);
}

bool UCSDeliveryOrderWidget::AcceptOrderById(const int32 OrderId, FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->AcceptDeliveryOrder(OrderId, OutMessage);
}

bool UCSDeliveryOrderWidget::DeclineOrderById(const int32 OrderId, FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->DeclineDeliveryOrder(OrderId, OutMessage);
}

bool UCSDeliveryOrderWidget::CancelOrderById(const int32 OrderId, FText& OutMessage)
{
	return IsValid(OrderTerminal) && OrderTerminal->CancelDeliveryOrder(OrderId, OutMessage);
}

void UCSDeliveryOrderWidget::CloseOrderWindow()
{
	if (IsValid(OrderTerminal))
	{
		OrderTerminal->CloseDeliveryOrderUI(this);
	}
}

void UCSDeliveryOrderWidget::NativeDestruct()
{
	if (IsValid(BoundDeliveryOrderSubsystem))
	{
		BoundDeliveryOrderSubsystem->OnDeliveryOrdersChanged.RemoveDynamic(
			this,
			&UCSDeliveryOrderWidget::HandleDeliveryOrdersChanged);
	}
	BoundDeliveryOrderSubsystem = nullptr;
	Super::NativeDestruct();
}

void UCSDeliveryOrderWidget::HandleDeliveryOrdersChanged()
{
	RefreshOrderList();
}
