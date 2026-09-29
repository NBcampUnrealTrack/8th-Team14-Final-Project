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
class ACSWeatherIndoorArea;
class ACSWeatherZoneActor;
class UCSWeatherSettingsDataAsset;
class UNiagaraSystem;

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

	// 실제 날씨 제한과 사용 안내가 같은 설정을 참조하도록 고정 맑음 기간을 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Weather")
	int32 GetFixedClearBusinessDays() const;

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

	// 오늘 날씨가 이 상품의 수요를 얼마나 바꾸는지 (가중치 곱하기 전 원시값, 합산 결과)
	// 규칙이 없거나 설정이 비어 있으면 0을 반환해 날씨 도입 전과 같은 동작이 됩니다.
	//
	// 오늘 날씨를 "미리 정해둔 활성 규칙 목록"으로 캐싱하지 않고 호출 시점에 GetCurrentWeather()로 읽습니다.
	// cs.WeatherSet은 CurrentWeather만 덮어쓰기 때문에, 캐싱하면 날씨는 비인데 수요는 맑음 기준으로 계산됩니다.
	UFUNCTION(BlueprintPure, Category = "Weather|Demand")
	float GetProductWeatherDemandModifier(const FDataTableRowHandle& ProductRow) const;

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

	// -----------------------------------------------------------------------
	// 실내·문 앞 연출 영역
	// 플레이어가 문 앞 판정 박스나 실내 영역 안에 있으면 캐릭터 주위 비·눈을 끄고
	// 모든 문 연출 영역을 보이게 합니다. 밖으로 나가면 반대로 바꿉니다.
	// -----------------------------------------------------------------------

	// 문 앞 연출 영역을 등록하고 현재 날씨 에셋과 표시 상태를 적용
	// 등록 후 플레이어의 보호 상태를 재판정
	void RegisterZone(ACSWeatherZoneActor* Zone);

	// 문 앞 연출 영역의 등록을 해제하고 유효하지 않은 항목도 정리
	void UnregisterZone(ACSWeatherZoneActor* Zone);

	// 실내 영역을 등록하고 현재 플레이어의 보호 상태를 재판정
	void RegisterIndoorArea(ACSWeatherIndoorArea* Area);

	// 실내 영역을 등록하고 현재 플레이어의 보호 상태를 재판정
	void UnregisterIndoorArea(ACSWeatherIndoorArea* Area);

	// 오버랩이 변경된 액터가 플레이어이면 보호 상태를 재판정
	// 손님과 직원 등 다른 액터의 오버랩은 무시
	void NotifyShelterOverlapChanged(AActor* OtherActor);

	// 플레이어가 실내 또는 문 앞 판정 영역에 걸쳐 있는지 반환
	// true이면 캐릭터 주위 파티클을 숨기고 문 앞 영역 파티클을 표시
	UFUNCTION(BlueprintPure, Category = "Weather|Shelter")
	bool IsPlayerSheltered() const { return bSheltered; }

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

	// DT_WeatherDemandRule을 날씨별로 미리 나눠둔 캐시
	// 캐싱 대상은 "표 데이터"이지 "오늘의 활성 규칙"이 아닙니다 - 오늘 날씨는 조회할 때마다 다시 읽습니다.
	// TMap<K, TArray<V>>는 UPROPERTY로 반영할 수 없지만, 행 구조체가 FName/float/enum뿐이라
	// UObject 참조가 없어 GC가 걷어갈 대상도 없습니다.
	TMap<ECSWeatherType, TArray<FCSWeatherDemandRuleRow>> WeatherDemandRuleCache;

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

	// 현재 날씨에서 문 앞 영역에 적용할 나이아가라 에셋
	// 사용할 에셋이 없으면 nullptr로 유지
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> ActiveZoneSystem;

	// 등록된 문 앞 연출 영역 목록
	// 액터의 수명을 유지하지 않도록 약한 참조로 보관
	TArray<TWeakObjectPtr<ACSWeatherZoneActor>> Zones;

	// 등록된 실내 판정 영역 목록
	// 액터의 수명을 유지하지 않도록 약한 참조로 보관
	TArray<TWeakObjectPtr<ACSWeatherIndoorArea>> IndoorAreas;

	// 플레이어가 실내 또는 문 앞 판정 영역에 걸쳐 있는 상태
	// 비와 눈의 재생 여부와 관계없이 판정 결과를 관리
	bool bSheltered = false;

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

	// 수요 규칙표를 날씨별로 나눠 캐시에 담습니다. 규칙표가 없으면 캐시를 비운 채 돌아옵니다.
	void BuildWeatherDemandRuleCache();

	// 시작 시 날씨 후보·가중치·규칙표 상태를 한 번만 찍습니다.
	// 규칙표가 연결 안 된 채로 넘어가면 보정이 전부 0이 되는데 게임은 멀쩡히 돌아가서
	// 눈에 띄지 않으므로, 그 실패를 시작 시점에 드러내려는 로그입니다.
	void LogWeatherStartupSummary() const;

	// 오늘 날씨에 걸리는 규칙이 몇 건인지 (로그용)
	int32 GetActiveDemandRuleCount() const;

	// 날씨에 맞춰 파티클(이펙트) 액터를 스폰하거나 파괴하고, 문 연출 영역 에셋도 교체
	void ApplyWeatherEffect(ECSWeatherType NewWeather);

	// 날씨 행의 이펙트 액터 클래스를 로드해 생성하고 연출 시작
	void SpawnEffectActor(const FCSWeatherRow* Row);

	// 날씨 행의 영역용 나이아가라 에셋을 로드해 등록된 모든 영역에 적용
	// 에셋이 없으면 기존 영역 파티클을 비활성화
	void ApplyZoneSystem(const FCSWeatherRow* Row);

	// -----------------------------------------------------------------------
	// 실내·문 앞 판정
	// -----------------------------------------------------------------------

	// 플레이어 위치를 재판정하고 상태가 변경되면 연출 표시를 갱신
	// bForceApply가 true이면 상태가 같아도 다시 적용
	// 플레이어 폰이 없으면 이전 상태를 유지
	void UpdateShelterState(bool bForceApply);

	// 등록된 실내 박스와 문 앞 판정 박스의 오버랩 상태를 조회
	// 플레이어가 하나라도 겹쳐 있으면 true를 반환
	bool EvaluateSheltered(const AActor* PlayerPawn) const;

	// 보호 상태이면 캐릭터 주위 파티클을 숨기고 등록된 모든 문 앞 영역을 표시
	// 보호 상태가 아니면 캐릭터 주위 파티클을 표시하고 문 앞 영역을 숨김
	void ApplyShelterState();

	// ----- 디버그 (임시) 여기부터 -----
	// 디버그 출력용. 한 엔트리를 한 줄 문자열과 색으로 바꿉니다.
	FString MakeDebugLine(const FCSWeatherForecastEntry& Entry) const;
	FColor GetDebugColor(const FCSWeatherForecastEntry& Entry) const;
	// ----- 디버그 (임시) 여기까지 -----
};
