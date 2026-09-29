#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSWeatherTypes.h"
#include "CSNoticeBoardWeatherTabWidget.generated.h"

class UCSWeatherSubsystem;
class UImage;
class UTexture2D;

// 날씨 서브시스템에서 조회한 오늘과 내일 날씨를 게시판 탭에 표시
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSNoticeBoardWeatherTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 오늘과 내일 날씨를 다시 조회해 블루프린트 화면에 전달
	UFUNCTION(BlueprintCallable, Category = "Notice Board|Weather")
	void RefreshWeatherTab();

	// 오늘과 내일 날씨 데이터를 받아 블루프린트에서 화면을 갱신
	// bHasTomorrowForecast로 내일 예보의 존재 여부를 전달
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Weather")
	void RefreshWeatherTabView(
		const FCSWeatherForecastEntry& TodayWeather,
		const FCSWeatherForecastEntry& TomorrowWeather,
		bool bHasTomorrowForecast);

	// 예보 갱신 이벤트를 구독하고 날씨 탭의 초기 화면을 갱신
	virtual void NativeConstruct() override;

	// 예보 갱신 이벤트 구독을 해제하고 서브시스템 참조를 정리
	virtual void NativeDestruct() override;

protected:
	/** WBP에 배치된 오늘/내일 날씨 아이콘. 이름이 일치하면 자동 연결됩니다. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_TodayWeather;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_TomorrowWeather;

	// 날씨 예보 갱신 알림을 받아 날씨 탭을 갱신
	UFUNCTION()
	void HandleWeatherForecastUpdated();

	// 날씨 조회와 예보 갱신 이벤트 구독에 사용할 서브시스템 참조를 보관
	UPROPERTY(Transient)
	TObjectPtr<UCSWeatherSubsystem> WeatherSubsystem;

private:
	void ApplyWeatherIcon(UImage* ImageWidget, ECSWeatherType WeatherType, bool bVisible) const;
	UTexture2D* ResolveWeatherIcon(ECSWeatherType WeatherType) const;
};
