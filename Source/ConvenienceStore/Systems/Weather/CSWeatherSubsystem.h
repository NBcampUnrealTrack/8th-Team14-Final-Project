#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSWeatherTypes.h"
#include "CSWeatherSubsystem.generated.h"

// 1이면 하루 시작 시 날씨를 화면에 자동 출력합니다.
// 게시판 위젯이 붙으면 0으로 바꿔 화면 출력만 끌 수 있습니다. 콘솔 커맨드는 남습니다.
#define CS_WEATHER_DEBUG 1

class ACSStoreGameState;
class ACSWeatherEffectActor;
class UCSWeatherSettingsDataAsset;

// 날씨가 바뀌었을 때 다른 시스템(손님, 조명 등)에 알리기 위한 델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnWeatherChanged,ECSWeatherType, NewWeather,ECSWeatherType, PreviousWeather);

// 날씨는 그대로라도 예보(내일, 모레 날씨)가 갱신되었음을 알림
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnWeatherForecastUpdated);

// 날씨의 결정·보관·전파·연출 교체를 소유하는 서브시스템
UCLASS()
class CONVENIENCESTORE_API UCSWeatherSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// -----------------------------------------------------------------------
	// 조회(다른 시스템이나 UI에서 날씨 정보를 가져갈 때 사용하는 함수)
	// -----------------------------------------------------------------------

	// 현재 날씨를 반환
	UFUNCTION(BlueprintPure, Category = "Weather")
	ECSWeatherType GetCurrentWeather() const { return CurrentWeather; }

	// 날씨 시스템이 인식하고 있는 현재 영업일차를 반환
	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetCurrentBusinessDay() const { return CurrentBusinessDay; }

	// 특정 일차(0 = 오늘, 1 = 내일)의 날씨를 조회
	// 설정한 "공개 예보 범위(PublicForecastDays)"를 넘어서 조회하려 하면 None을 반환
	UFUNCTION(BlueprintCallable, Category = "Weather")
	ECSWeatherType GetForecastWeather(int32 DayOffset);

	// 오늘을 포함해 UI 게시판에 띄울 예보 목록 전체를 구조체 정보로 만들어 반환
	UFUNCTION(BlueprintCallable, Category = "Weather|Board")
	TArray<FCSWeatherForecastEntry> GetForecastEntries();

	// 오늘 날씨에 대한 구조체 정보만 반환
	UFUNCTION(BlueprintCallable, Category = "Weather")
	FCSWeatherForecastEntry GetCurrentWeatherEntry();

	// 데이터에 설정된 "게시판에 공개할 미래 일수"를 가져옴
	UFUNCTION(BlueprintPure, Category = "Weather|Board")
	int32 GetPublicForecastDays() const;

	// -----------------------------------------------------------------------
	// 알림 (이벤트 브로드캐스트)
	// -----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Weather")
	FCSOnWeatherChanged OnWeatherChanged;

	UPROPERTY(BlueprintAssignable, Category = "Weather|Board")
	FCSOnWeatherForecastUpdated OnWeatherForecastUpdated;

	// -----------------------------------------------------------------------
	// 확률 조정 - 영구
	// -----------------------------------------------------------------------

	// 영구층에서 특정 날씨의 가중치를 확률(0~1)로 환산해 반환
	UFUNCTION(BlueprintPure, Category = "Weather|Roll")
	float GetWeatherChance(ECSWeatherType Type) const;

	// 영구층에서 특정 날씨의 확률을 조정합니다. 기간제와 동일한 연산을 사용
	// 이미 확정된 날씨는 바뀌지 않으며, 이후 확률 계산부터 반영
	//
	// Op별 Value의 의미:
	//   Set      - 목표 확률(0~1)을 지정합니다. 확률이 0이던 날씨도 등장시킬 수 있습니다.
	//   Multiply - 현재 확률에 배수를 곱하고, 결과를 0~1로 제한합니다.
	//              100%에 도달하면 다른 날씨의 확률은 모두 0이 됩니다.
	//   Weight   - 해당 가중치에 배수를 곱한 뒤 표 전체를 정규화합니다.
	//              예를 들어 2를 넣어도 최종 확률이 정확히 2배가 되지는 않습니다.
	//
	// Mode는 Set·Multiply에서 나머지 날씨의 확률을 재분배하는 방식입니다.
	// Weight는 전체 정규화를 사용하므로 Mode를 사용하지 않습니다.
	// 현재 확률이 0이면 Multiply·Weight 요청은 경고를 남기고 거부합니다.
	UFUNCTION(BlueprintCallable, Category = "Weather|Roll")
	void ApplyWeatherChance(ECSWeatherType Type, ECSChanceOpType Op, float Value,
		ECSChanceRebalanceMode Mode = ECSChanceRebalanceMode::Proportional);

	// 영구층의 가중치 표 전체를 복사해서 반환
	// 기간제 레이어는 포함X
	UFUNCTION(BlueprintPure, Category = "Weather|Roll")
	FCSWeatherWeightSet GetRuntimeWeights() const { return RuntimeWeights; }

	// 영구층의 가중치 표 전체를 교체하고 정규화
	// 챕터 전환처럼 기본 확률 구성을 통째로 바꿀 때 사용
	// 이미 확정된 날씨는 바뀌지 않으며, 등록된 기간제 레이어는 유지
	UFUNCTION(BlueprintCallable, Category = "Weather|Roll")
	void SetRuntimeWeights(const FCSWeatherWeightSet& NewWeights);

	// -----------------------------------------------------------------------
	// 확률 조정 - 기간제
	// 영구층을 직접 바꾸지 않고, 지정한 기간의 확률 계산에만 반영합니다.
	// 기간이 지나면 계산에서 제외되므로 별도의 원상 복구가 필요 없습니다.
	// -----------------------------------------------------------------------


	// 기간제 확률 조정을 등록합니다. 같은 SourceId가 있으면 교체
	// 이미 확정된 날씨는 바뀌지 않으므로, 기간 안에서 새로 추첨하는 날짜에만 영향을 줌
	UFUNCTION(BlueprintCallable, Category = "Weather|Roll")
	void AddChanceLayer(const FCSWeatherChanceLayer& Layer);

	// SourceId가 일치하는 기간제 레이어를 제거합니다. 없으면 아무 작업도 하지 않습니다.
	// 이후 확률 계산에서 해당 효과가 빠지며, 이미 확정된 날씨는 바뀌지 않습니다.
	UFUNCTION(BlueprintCallable, Category = "Weather|Roll")
	void RemoveChanceLayer(FName SourceId);

	// 지정한 영업 일차의 영구층 + 활성 기간제 레이어를 합산한 확률을 반환
	UFUNCTION(BlueprintPure, Category = "Weather|Roll")
	float GetEffectiveChance(ECSWeatherType Type, int32 BusinessDay) const;

	// -----------------------------------------------------------------------
	// 이벤트 개입 - 확정된 날씨 변경
	// -----------------------------------------------------------------------

	// 오늘 또는 공개된 미래 예보의 날씨를 직접 변경
	// DayOffset은 오늘 기준입니다. (0 = 오늘, 1 = 내일)
	UFUNCTION(BlueprintCallable, Category = "Weather|Event")
	bool SetForecastWeather(int32 DayOffset, ECSWeatherType NewWeather);

	// ----- 디버그 (임시) 여기부터 -----
	// 구현은 전부 Systems/Weather/CSWeatherDebug.cpp에 있습니다.
	// 제거할 때는 이 블록과 그 파일, CSWeatherSubsystem.cpp의 #if CS_WEATHER_DEBUG 블록을 지웁니다.

	// 오늘 + 공개 예보 전체를 화면에 출력합니다. UI가 없으므로 이게 유일한 표시 수단입니다.
	// 줄 수는 GetPublicForecastDays()에 따라 자동으로 늘어납니다.
	UFUNCTION(BlueprintCallable, Category = "Weather|Debug")
	void DebugPrintForecast(float DisplayDuration = 8.0f);

	// 오늘 날씨를 강제로 바꿉니다. 확정 배열도 함께 덮어써서 예보와 어긋나지 않게 합니다.
	UFUNCTION(BlueprintCallable, Category = "Weather|Debug")
	void DebugSetTodayWeather(ECSWeatherType NewWeather);

	// 날씨 쪽 일차만 하루 앞으로 밀어 전환 연출을 미리 봅니다.
	// GameState의 일차는 바꾸지 않으므로 실제 하루가 넘어가면 다시 맞춰집니다.
	UFUNCTION(BlueprintCallable, Category = "Weather|Debug")
	void DebugAdvanceDayPreview();
	// ----- 디버그 (임시) 여기까지 -----

protected:
	// 과거부터 미래까지 확정된 날씨를 순서대로 쌓아두는 배열
	// append-only이며 한 번 정해진 날짜는 다시 굴리지 않습니다.
	UPROPERTY(Transient)
	TArray<ECSWeatherType> DecidedWeather;

	// 현재 캐싱된 오늘 날씨
	UPROPERTY(Transient)
	ECSWeatherType CurrentWeather = ECSWeatherType::Clear;

	// 현재 화면에 띄워져 있는 비/눈 파티클 액터
	UPROPERTY(Transient)
	TObjectPtr<ACSWeatherEffectActor> ActiveEffectActor;

	// 데이터 에셋을 들고 있는 포인터
	UPROPERTY(Transient)
	TObjectPtr<UCSWeatherSettingsDataAsset> Settings;

	// 현재 일차를 파악하기 위해 GameState를 참조
	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> StoreGameState;

	// 게임 시작 시 데이터 테이블(DT_Weather)을 미리 읽어둔 캐시 맵
	// 매번 테이블을 검색해 성능이 떨어지는 것을 막음
	UPROPERTY(Transient)
	TMap<ECSWeatherType, FCSWeatherRow> WeatherRowCache;

	// 저장할 날씨 확률표입니다. 시작 시 Settings->BaseWeights를 복사해옵니다.
	// 영구 이벤트는 이 표를 직접 고칩니다. 되돌리지 않습니다.
	UPROPERTY(Transient)
	FCSWeatherWeightSet RuntimeWeights;

	// 기간제 확률 지정 목록입니다. 매 계산마다 영구층 사본 위에 다시 적용됩니다.
	// 영구층 자체는 절대 건드리지 않으므로, 만료되면 별도 복원 없이 원래대로 돌아옵니다.
	UPROPERTY(Transient)
	TArray<FCSWeatherChanceLayer> ChanceLayers;

	// GameState와 동일하게 맞추고 있는 오늘 일차
	int32 CurrentBusinessDay = 1;

private:
	// GameState에서 하루가 넘어갈 때 호출되는 함수
	UFUNCTION()
	void HandleBusinessDayStarted(int32 NewBusinessDay);
	void ApplyBusinessDay(int32 BusinessDay);

	// BusinessDay 일차까지 확정 배열을 미리 채워둠
	void EnsureDecidedThrough(int32 BusinessDay);

	// 배율 등을 모두 합쳐서 "그날 하루의 최종 확률표"를 계산 (합계 1.0)
	void BuildDayWeights(int32 BusinessDay,TArray<ECSWeatherType>& OutTypes,TArray<float>& OutNormalizedWeights) const;

	// 특정 일차 하루치의 확률표를 계산해서 돌려줍니다.
	FCSWeatherWeightSet BuildEffectiveWeights(int32 BusinessDay) const;

	// -----------------------------------------------------------------------
	// 확률표 연산
	// -----------------------------------------------------------------------

	// 확률 변경 요청을 실제 확률표에 적용하는 공통 진입점
	// Op에 따라 처리 방식이 달라집니다.(Set, Multiply, Weight)
	static void ApplyOpTo(FCSWeatherWeightSet& Weights, ECSWeatherType Type,ECSChanceOpType Op, float Value, ECSChanceRebalanceMode Mode);

	// 확률표에서 특정 날씨가 현재 차지하는 "실제 확률"을 구합니다.
	static float GetChanceIn(const FCSWeatherWeightSet& Weights, ECSWeatherType Type);

	// 특정 날씨의 확률을 NewChance로 변경하고, 그 변화량만큼 나머지 날씨의 확률을 다시 배분
	static void ApplyChanceTo(FCSWeatherWeightSet& Weights, ECSWeatherType Type,float NewChance, ECSChanceRebalanceMode Mode);

	// 확률표의 모든 값을 합쳐서 1.0이 되도록 다시 맞춥니다.
	static void NormalizeSet(FCSWeatherWeightSet& Weights);

	// 대상 날씨의 확률을 변경한 뒤,"나머지 날씨끼리의 기존 비율"을 유지하면서 재분배
	static void RebalanceProportionalIn(FCSWeatherWeightSet& Weights,ECSWeatherType Type, float NewChance);
	// 대상 날씨의 확률 변화량을 나머지 날씨들에게 가능한 한 같은 양씩 나누어 반영
	static void RebalanceEvenIn(FCSWeatherWeightSet& Weights,ECSWeatherType Type, float Delta);

	// -----------------------------------------------------------------------


	// 확정 배열에서 특정 일차의 날씨를 꺼내옴
	ECSWeatherType RollWeather(int32 BusinessDay) const;

	// UI에 전달할 날씨 정보 구조체를 만듭니다
	ECSWeatherType GetDecidedWeather(int32 BusinessDay) const;
	FCSWeatherForecastEntry MakeForecastEntry(int32 DayOffset) const;

	// 캐시 생성 및 검색 헬퍼 함수
	void BuildWeatherRowCache();
	const FCSWeatherRow* FindWeatherRow(ECSWeatherType Type) const;

	// 날씨에 맞춰 파티클(이펙트) 액터를 스폰하거나 파괴
	void ApplyWeatherEffect(ECSWeatherType NewWeather);

	// ----- 디버그 (임시) 여기부터 -----
	// 디버그 출력용. 한 엔트리를 한 줄 문자열과 색으로 바꿉니다.
	FString MakeDebugLine(const FCSWeatherForecastEntry& Entry) const;
	FColor GetDebugColor(const FCSWeatherForecastEntry& Entry) const;
	// ----- 디버그 (임시) 여기까지 -----
};
