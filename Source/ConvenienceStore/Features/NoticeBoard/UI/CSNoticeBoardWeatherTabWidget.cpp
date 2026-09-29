#include "Features/NoticeBoard/UI/CSNoticeBoardWeatherTabWidget.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"
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
		ApplyWeatherIcon(Image_TodayWeather, ECSWeatherType::None, false);
		ApplyWeatherIcon(Image_TomorrowWeather, ECSWeatherType::None, false);
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

	ApplyWeatherIcon(Image_TodayWeather, TodayWeather.WeatherType, Entries.IsValidIndex(0));
	ApplyWeatherIcon(Image_TomorrowWeather, TomorrowWeather.WeatherType, bHasTomorrowForecast);

	// 오늘과 내일 날씨를 블루프린트 화면에 전달
	RefreshWeatherTabView(TodayWeather, TomorrowWeather, bHasTomorrowForecast);
}

void UCSNoticeBoardWeatherTabWidget::ApplyWeatherIcon(
	UImage* ImageWidget,
	const ECSWeatherType WeatherType,
	const bool bVisible) const
{
	if (!IsValid(ImageWidget))
	{
		return;
	}

	UTexture2D* Icon = bVisible ? ResolveWeatherIcon(WeatherType) : nullptr;
	if (!IsValid(Icon))
	{
		ImageWidget->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ImageWidget->SetBrushFromTexture(Icon, false);
	ImageWidget->SetVisibility(ESlateVisibility::Visible);
}

UTexture2D* UCSNoticeBoardWeatherTabWidget::ResolveWeatherIcon(const ECSWeatherType WeatherType) const
{
	const TCHAR* AssetPath = nullptr;
	switch (WeatherType)
	{
	case ECSWeatherType::Clear:
		AssetPath = TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_sunicon_.T_Board_sunicon_");
		break;
	case ECSWeatherType::Rain:
		AssetPath = TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_rainicon_.T_Board_rainicon_");
		break;
	case ECSWeatherType::Snow:
		AssetPath = TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_snowicon_.T_Board_snowicon_");
		break;
	case ECSWeatherType::Cloudy:
		AssetPath = TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_Cloudicon.T_Board_Cloudicon");
		break;
	case ECSWeatherType::Heat:
		AssetPath = TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_sunnyicon_.T_Board_sunnyicon_");
		break;
	case ECSWeatherType::Cold:
		AssetPath = TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_windyicon_.T_Board_windyicon_");
		break;
	default:
		return nullptr;
	}

	return LoadObject<UTexture2D>(nullptr, AssetPath);
}

void UCSNoticeBoardWeatherTabWidget::HandleWeatherForecastUpdated()
{
	RefreshWeatherTab();
}
