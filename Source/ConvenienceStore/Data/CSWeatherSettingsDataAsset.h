#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CSWeatherTypes.h"
#include "CSWeatherSettingsDataAsset.generated.h"

class UDataTable;

// 날씨 확률과 예보 공개 범위를 소유하는 설정 에셋
// 게임모드 Store|Weather 카테고리에 지정하면 날씨 서브시스템이 시작 시점에 가져감
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSWeatherSettingsDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// 날씨 종류별 표시·연출 데이터 테이블
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UDataTable> WeatherTable;

	// 날씨별 기본 확률입니다. (총합 1.0)
	// 비워두면 경고 로그를 남기고 날씨가 맑음으로 유지
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Roll")
	FCSWeatherWeightSet BaseWeights;

	// 게시판에 공개할 미래 일수입니다.
	// 1이면 내일까지, 7이면 일주일 뒤까지입니다.
	// 이 값만 바꾸면 C++ 수정 없이 공개 범위 수정가능
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Forecast",
		meta = (ClampMin = "0", ClampMax = "14", UIMin = "0", UIMax = "14"))
	int32 PublicForecastDays = 1;

	// 이 일차까지는 항상 맑음으로 고정
	// 1이면 1일차만 고정함 (만약 날씨 튜토리얼이나 원하는 위치부터 날씨가 생성되길 원하면 일수 조정가능)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Forecast",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 FixedClearBusinessDays = 1;

	// 이펙트 액터를 스폰할 위치
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Effect")
	FVector EffectSpawnOffset = FVector::ZeroVector;
};
