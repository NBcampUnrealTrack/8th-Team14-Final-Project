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
	// 확률 배율 (계절, 이벤트 등이 개입하는 통로)
	// -----------------------------------------------------------------------

	// 계절이나 이벤트가 날씨 확률을 조작할 때 배율을 등록하는 함수
	// 이미 보여준 예보(약속)는 뒤집지 않고, 아직 안 보여준 미래의 날씨 확률만 바꿉니다[cite: 8].
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void AddWeightModifier(const FCSWeatherWeightModifier& Modifier);

	// 등록했던 배율을 이름표(SourceId)로 찾아 제거
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void RemoveWeightModifier(FName SourceId);

	// 배율이 등록/해제되었을 때, 대중에게 공개되지 않은 먼 미래의 날씨 기록을 버리고 새 확률로 다시 뽑음
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void InvalidateUnpublishedTail();

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

	// 현재 적용 중인 확률 배율들을 모아두는 맵
	UPROPERTY(Transient)
	TMap<FName, FCSWeatherWeightModifier> WeightModifiers;

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
