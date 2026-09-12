#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSWeatherTypes.h"
#include "CSNoticeBoardDisplayWidget.generated.h"

class UCSDeliveryOrderSubsystem;
class UCSWeatherSubsystem;

/** 게시판 표면에 항상 표시되는 월드 UI의 공통 기반 클래스입니다. */
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSNoticeBoardDisplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Display")
	void RefreshDisplay();

	UFUNCTION(BlueprintPure, Category = "Notice Board|Display")
	bool HasPendingDeliveryOrder() const;

	/** 배달 대기 알림 룬의 표시 여부를 BP에서 갱신합니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Display")
	void RefreshNoticeBoardDisplay(bool bHasPendingDeliveryOrder);

	// 오늘 날씨 데이터를 전달 [백무송]
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Display")
	void RefreshTodayWeatherDisplay(const FCSWeatherForecastEntry& TodayWeather);

protected:
	UFUNCTION()
	void HandleDeliveryOrdersChanged();

	// 날씨 예보 갱신 알림을 받아 오늘 날씨 표시를 갱신
	UFUNCTION()
	void HandleWeatherForecastUpdated();

	// 오늘 날씨를 조회해 게시판 표면의 날씨 표시를 갱신
	void RefreshTodayWeather();

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderSubsystem> DeliveryOrderSubsystem;

	// 날씨 조회와 예보 갱신 이벤트 구독에 사용할 서브시스템 참조를 보관
	UPROPERTY(Transient)
	TObjectPtr<UCSWeatherSubsystem> WeatherSubsystem;
};
