#include "Features/NoticeBoard/UI/CSNoticeBoardDisplayWidget.h"

#include "Data/CSDeliveryOrderTypes.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"

void UCSNoticeBoardDisplayWidget::NativeConstruct()
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
	RefreshDisplay();
}

void UCSNoticeBoardDisplayWidget::NativeDestruct()
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

void UCSNoticeBoardDisplayWidget::RefreshDisplay()
{
	RefreshNoticeBoardDisplay(HasPendingDeliveryOrder());
}

bool UCSNoticeBoardDisplayWidget::HasPendingDeliveryOrder() const
{
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->GetDeliveryOrders().ContainsByPredicate(
		[](const FCSDeliveryOrderRuntime& Order)
		{
			return Order.Status == ECSDeliveryOrderStatus::PendingAcceptance;
		});
}

void UCSNoticeBoardDisplayWidget::HandleDeliveryOrdersChanged()
{
	RefreshDisplay();
}
