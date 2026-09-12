#include "Features/NoticeBoard/UI/CSNoticeBoardWeatherTabWidget.h"

#include "Systems/Weather/CSWeatherSubsystem.h"

void UCSNoticeBoardWeatherTabWidget::NativeConstruct()
{
	Super::NativeConstruct();

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
	RefreshWeatherTab();
}

void UCSNoticeBoardWeatherTabWidget::NativeDestruct()
{
	if (IsValid(WeatherSubsystem))
	{
		WeatherSubsystem->OnWeatherForecastUpdated.RemoveDynamic(
			this,
			&ThisClass::HandleWeatherForecastUpdated);
	}
	WeatherSubsystem = nullptr;
	Super::NativeDestruct();
}

void UCSNoticeBoardWeatherTabWidget::RefreshWeatherTab()
{
	if (!IsValid(WeatherSubsystem))
	{
		RefreshWeatherTabView(FCSWeatherForecastEntry(), FCSWeatherForecastEntry(), false);
		return;
	}

	// 오늘과 내일 예보를 조회하고 내일 항목의 존재 여부를 확인
	const TArray<FCSWeatherForecastEntry> Entries = WeatherSubsystem->GetForecastEntries();
	const bool bHasTomorrowForecast = Entries.IsValidIndex(1);

	// 오늘 예보가 있으면 해당 데이터를 사용하고 없으면 빈 데이터를 사용
	FCSWeatherForecastEntry TodayWeather;
	if (Entries.IsValidIndex(0))
	{
		TodayWeather = Entries[0];
	}
	else
	{
		TodayWeather = FCSWeatherForecastEntry();
	}

	// 내일 예보가 있으면 해당 데이터를 사용하고 없으면 빈 데이터를 사용
	FCSWeatherForecastEntry TomorrowWeather;
	if (bHasTomorrowForecast)
	{
		TomorrowWeather = Entries[1];
	}
	else
	{
		TomorrowWeather = FCSWeatherForecastEntry();
	}

	// 오늘과 내일 날씨를 블루프린트 화면에 전달
	RefreshWeatherTabView(TodayWeather, TomorrowWeather, bHasTomorrowForecast);
}

void UCSNoticeBoardWeatherTabWidget::HandleWeatherForecastUpdated()
{
	RefreshWeatherTab();
}
