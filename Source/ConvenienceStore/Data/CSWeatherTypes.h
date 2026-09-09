#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSWeatherTypes.generated.h"

class ACSWeatherEffectActor;

// 날씨 종류
// 새 날씨를 추가할 때는 Max 앞에 값을 넣고 DT_Weather에 같은 이름의 행을 추가
UENUM(BlueprintType)
enum class ECSWeatherType : uint8
{
	None    UMETA(Hidden),

	// VFX 보유. 1차 구현 대상
	Clear   UMETA(DisplayName = "맑음"),   // 이펙트 없음이 정상
	Rain    UMETA(DisplayName = "비"),     // NS_Rain
	Snow    UMETA(DisplayName = "눈"),     // NS_Snow

	// 아래는 자리만 확보. DT_Weather 행이 준비되면 순차 활성화
	Cloudy  UMETA(DisplayName = "흐림"),
	Heat    UMETA(DisplayName = "폭염"),
	Cold    UMETA(DisplayName = "한파"),

	Max     UMETA(Hidden)
};

// DT_Weather의 한 행입니다.
// 행 이름은 ECSWeatherType의 값 이름과 같아야 합니다.
// 이 행은 표시와 연출만 갖습니다. 확률은 DA_WeatherSettings.BaseWeights가 단독으로 소유합니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	ECSWeatherType WeatherType = ECSWeatherType::Clear;

	// 게시판과 HUD에 표시할 이름입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Display")
	FText DisplayName;

	// 비워두면 이펙트 없는 날씨입니다. 비어 있어도 시스템은 정상 동작합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Effect")
	TSoftClassPtr<ACSWeatherEffectActor> EffectActorClass;
};

// 날씨 종류별 확률입니다. (총합 1.0이 규칙)
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherWeightSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Roll")
	TMap<ECSWeatherType, float> Weights;

	// 적히지 않은 종류는 확률 0입니다. 즉 DT_Weather에 행이 있어도 등장 X
	// 새 날씨를 자리만 잡아두고 아직 안 내보낼 때 이 성질을 이용
	float GetWeight(ECSWeatherType Type) const
	{
		const float* Found = Weights.Find(Type);
		if (Found != nullptr)
		{
			// 값을 찾았다면, 그 값(*Found)과 0.0f 중 더 큰 값을 반환
			// (음수 값이 들어오는 것을 방지하기 위함)
			return FMath::Max(0.0f, *Found);
		}
		else
		{
			// 값을 찾지 못했다면 기본값인 0.0f를 반환합니다.
			return 0.0f;
		}
	}
};

// 다른 시스템이 날씨 확률에 개입하는 통로
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherWeightModifier
{
	GENERATED_BODY()


	//외부에서 누가 확률에 영향을 주는지 나타내는 이름표
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll")
	FName SourceId;

	// 특정 날씨의 상대 가중치를 몇 배로 만들지 정하는 목록입니다.
	// 최종 확률 배율이 아닙니다. 정규화를 거치므로 배율을 2로 줘도 확률이 2배가 되지는 않습니다.
	//   예) 0.3 / 0.3 / 0.4 에서 첫 번째만 2배 -> 0.6 / 0.3 / 0.4 -> 46.2% / 23.1% / 30.8%
	// 배율 없는 날씨도 정규화에 휩쓸려 함께 깎입니다. 희귀한 날씨일수록 배율이 의도대로 먹습니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll")
	TMap<ECSWeatherType, float> Multipliers;

	// 확률 수정 적용 시작 영업일차입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 StartBusinessDay = 1;

	// 확률 수정 적용 종료 영업일차(포함)입니다. 0이면 무기한입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 EndBusinessDay = 0;

	bool IsActiveOn(int32 BusinessDay) const
	{
		if (BusinessDay < StartBusinessDay)
		{
			return false;
		}
		return EndBusinessDay <= 0 || BusinessDay <= EndBusinessDay;
	}
};

// 게시판·HUD 전달용
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherForecastEntry
{
	GENERATED_BODY()

	// 0 = 오늘, 1 = 내일. 게시판이 "내일", "3일 뒤" 문구를 만들 때 씁니다.
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	int32 DayOffset = 0;

	// 실제 영업일차입니다. ACSStoreGameState::CurrentBusinessDay와 같은 기준(1부터)입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	int32 BusinessDay = 1;

	// 어떤 날씨인지 나타내는 값
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	ECSWeatherType WeatherType = ECSWeatherType::Clear;

	// UI 화면에 보여줄 이름
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FText DisplayName;
};
