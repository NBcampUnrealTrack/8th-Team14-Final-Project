#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSWeatherTypes.generated.h"

class ACSWeatherEffectActor;

// 날씨 종류
// 새 날씨는 Max 앞에 추가하고, DT_Weather에 같은 이름의 행을 등록
UENUM(BlueprintType)
enum class ECSWeatherType : uint8
{
    // 날씨가 없거나 유효하지 않은 조회 결과를 나타내는 값
	None    UMETA(Hidden),

	// VFX 보유. 1차 구현 대상
	Clear   UMETA(DisplayName = "맑음"),   // 이펙트 없음이 정상
	Rain    UMETA(DisplayName = "비"),     // NS_Rain
	Snow    UMETA(DisplayName = "눈"),     // NS_Snow

	// 추후 사용할 날씨 종류
	// 실제 등장하려면 날씨 데이터 행과 양수 가중치가 필요
	Cloudy  UMETA(DisplayName = "흐림"),
	Heat    UMETA(DisplayName = "폭염"),
	Cold    UMETA(DisplayName = "한파"),

	Max     UMETA(Hidden)
};

// DT_Weather에 저장할 날씨 데이터 행
// 행 이름은 ECSWeatherType의 값 이름과 동일하게 지정
// 날씨 종류와 표시 이름, 이펙트를 보관하며 기본 가중치는 날씨 설정에서 관리
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherRow : public FTableRowBase
{
	GENERATED_BODY()

    // 이 행이 나타내는 날씨 종류
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	ECSWeatherType WeatherType = ECSWeatherType::Clear;

	// 게시판과 HUD에 표시할 이름입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Display")
	FText DisplayName;

	// 해당 날씨에서 생성할 이펙트 액터 클래스
	// 비어 있으면 이펙트 없이 날씨를 적용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Effect")
	TSoftClassPtr<ACSWeatherEffectActor> EffectActorClass;
};

// 날씨 종류별 가중치를 보관하는 표
// 저장된 값의 합이 반드시 1일 필요는 없으며, 확률 계산 시 전체 합으로 나누어 사용
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherWeightSet
{
	GENERATED_BODY()

	// 날씨 종류를 키로 사용하고 해당 날씨의 가중치를 값으로 저장
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Roll")
	TMap<ECSWeatherType, float> Weights;

	// 특정 날씨의 가중치를 반환
	// 등록되지 않은 날씨나 음수 가중치는 0을 반환하며, 저장된 값은 변경하지 않음
	float GetWeight(ECSWeatherType Type) const
	{
		const float* Found = Weights.Find(Type);
		if (Found != nullptr)
		{
			// 저장된 값이 음수이면 반환값만 0으로 보정
			return FMath::Max(0.0f, *Found);
		}
		else
		{
			// 등록되지 않은 날씨는 가중치 0으로 처리
			return 0.0f;
		}
	}
};

// 대상 날씨의 확률을 변경한 뒤 나머지 날씨를 재분배하는 방식
UENUM(BlueprintType)
enum class ECSChanceRebalanceMode : uint8
{
	// 다른 날씨에 같은 배율을 적용해 서로의 기존 비율을 유지
	Proportional UMETA(DisplayName = "비례 배분"),

	// 다른 날씨에 같은 양을 더하거나 빼서 재분배
	// 회수 시 보유한 값까지만 차감하고 부족한 몫은 나머지 날씨가 부담
	Even         UMETA(DisplayName = "균등 배분"),
};

// 날씨의 확률 또는 가중치를 조정하는 연산 종류
UENUM(BlueprintType)
enum class ECSChanceOpType : uint8
{
	// Value를 목표 확률로 지정하고 나머지 날씨를 재분배
	// 확률이 0이던 날씨도 양수 값으로 설정 가능
	Set      UMETA(DisplayName = "확률 지정"),

	// 현재 확률에 Value를 곱한 값을 목표로 삼고 나머지 날씨를 재분배
	// 목표 확률은 0~1로 제한하며, 현재 확률이 0이면 곱해도 0
	Multiply UMETA(DisplayName = "확률 배수"),

	// 대상 가중치에 Value를 곱한 뒤 표 전체를 정규화
	// 최종 확률이 정확히 Value배가 되지는 않으며, 재분배 Mode는 사용하지 않음
	Weight   UMETA(DisplayName = "가중치 배율"),
};

// 지정한 기간에 적용할 확률 조정 한 건
// 영구층 사본에 적용하며, 기간이 지나면 계산에서 제외
// 이미 확정된 날씨는 변경하지 않음
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherChanceLayer
{
	GENERATED_BODY()

	// 레이어를 구분하는 식별자이며, 등록 시 빈 값은 허용하지 않음
	// 같은 식별자로 등록하면 교체하며, 제거할 때도 이 값을 사용
	// 여러 레이어를 계산할 때는 이 값의 사전순으로 적용
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll")
	FName SourceId;

	// 확률을 조정할 날씨 종류
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll")
	ECSWeatherType WeatherType = ECSWeatherType::Clear;

	// 확률 또는 가중치를 조정할 연산 방식
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll")
	ECSChanceOpType Op = ECSChanceOpType::Set;

	// Set이면 목표 확률(0~1), Multiply와 Weight이면 배수
	// 계산 결과 대신 연산에 사용할 값을 저장
	// 확률 계산마다 영구층과 앞선 레이어가 반영된 표에 적용
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll",
		meta = (ClampMin = "0.0"))
	float Value = 0.0f;

	// Set과 Multiply에서 나머지 날씨의 확률을 재분배하는 방식
	// Weight는 전체 정규화를 사용하므로 이 값을 사용하지 않음
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll")
	ECSChanceRebalanceMode Mode = ECSChanceRebalanceMode::Proportional;

	// 적용을 시작할 영업 일차이며, 해당 일차부터 포함
	// 오늘 기준 오프셋이 아닌 실제 영업 일차로 지정
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 StartBusinessDay = 1;

	// 적용을 종료할 영업 일차이며, 해당 일차까지 포함
	// 0이면 시작일 이후 직접 제거할 때까지 무기한 적용
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Roll",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 EndBusinessDay = 0;

	// 지정한 영업 일차가 레이어의 적용 기간에 포함되는지 반환
	bool IsActiveOn(int32 BusinessDay) const
	{
		// 시작일 이전이면 적용하지 않음
		if (BusinessDay < StartBusinessDay)
		{
			return false;
		}
		// 종료일이 0 이하이면 무기한, 양수이면 종료일까지만 적용
		return EndBusinessDay <= 0 || BusinessDay <= EndBusinessDay;
	}
};

// 게시판과 HUD에 전달할 확정 날씨 정보
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSWeatherForecastEntry
{
	GENERATED_BODY()

	// 오늘 기준 날짜 차이이며, 0 = 오늘, 1 = 내일
	// UI에서 내일 또는 며칠 뒤인지 표시할 때 사용
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	int32 DayOffset = 0;

	// 해당 예보의 실제 영업 일차이며, 1일부터 시작
	// 게임 상태의 CurrentBusinessDay와 같은 기준을 사용
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	int32 BusinessDay = 1;

	// 해당 일차에 확정된 날씨 종류
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	ECSWeatherType WeatherType = ECSWeatherType::Clear;

	// UI에 표시할 날씨 이름
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FText DisplayName;
};
