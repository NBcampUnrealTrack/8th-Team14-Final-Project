#include "Features/NoticeBoard/UI/CSNoticeBoardDisplayWidget.h"

#include "Data/CSDeliveryOrderTypes.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/Weather/CSWeatherSubsystem.h"

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


	if (GetWorld() == nullptr)
	{
		WeatherSubsystem = nullptr;
	}
	else
	{
		WeatherSubsystem = GetWorld()->GetSubsystem<UCSWeatherSubsystem>();
	}
	if (IsValid(WeatherSubsystem))
	{
		// 내일 예보만 변경된 경우에도 화면을 갱신하도록 예보 갱신 이벤트를 구독
		WeatherSubsystem->OnWeatherForecastUpdated.AddUniqueDynamic(
			this,
			&ThisClass::HandleWeatherForecastUpdated);
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

	if (IsValid(WeatherSubsystem))
	{
		WeatherSubsystem->OnWeatherForecastUpdated.RemoveDynamic(this,&ThisClass::HandleWeatherForecastUpdated);
	}
	WeatherSubsystem = nullptr;
	Super::NativeDestruct();
}

void UCSNoticeBoardDisplayWidget::RefreshDisplay()
{
	RefreshNoticeBoardDisplay(HasPendingDeliveryOrder());
	RefreshTodayWeather();
}

bool UCSNoticeBoardDisplayWidget::HasPendingDeliveryOrder() const
{
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->GetDeliveryOrders().ContainsByPredicate(
		[](const FCSDeliveryOrderRuntime& Order)
		{
			return Order.Status == ECSDeliveryOrderStatus::PendingAcceptance;
		});
}

void UCSNoticeBoardDisplayWidget::RefreshTodayWeather()
{
	// 날씨 서브시스템이 유효하면 오늘 날씨를 전달하고 없으면 빈 데이터를 전달
	if (IsValid(WeatherSubsystem))
	{
		RefreshTodayWeatherDisplay(WeatherSubsystem->GetCurrentWeatherEntry());
	}
	else
	{
		RefreshTodayWeatherDisplay(FCSWeatherForecastEntry());
	}
}

void UCSNoticeBoardDisplayWidget::HandleDeliveryOrdersChanged()
{
	RefreshDisplay();
}

void UCSNoticeBoardDisplayWidget::HandleWeatherForecastUpdated()
{
	RefreshTodayWeather();
}
