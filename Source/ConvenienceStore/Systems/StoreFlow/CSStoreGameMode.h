#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSStoreGameMode.generated.h"

class ACSStoreGameState;
class UDataTable;
class UCSAudioConfigDataAsset;
class UCSStoreEconomyDataAsset;
class UCSStoreProgressionDataAsset;
class UCSFreshnessSettingsDataAsset;
class UCSWeatherSettingsDataAsset;
class UCSTutorialBannerWidgetBase;
class UCSTutorialDialogueWidgetBase;
class UCSTutorialObjectiveWidgetBase;

// 준비, 영업, 마감 업무, 다음 날 상태로 이어지는 하루 흐름을 제어합니다.
UCLASS()
class CONVENIENCESTORE_API ACSStoreGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACSStoreGameMode();

	UFUNCTION(BlueprintCallable, Category = "Store")
	void StartStoreFlow();

	UFUNCTION(BlueprintCallable, Category = "Store")
	void StartPhase(ECStorePhase NewPhase);

	UFUNCTION(BlueprintCallable, Category = "Store|Business")
	bool RequestBusinessOpenState(bool bShouldOpen);

	UFUNCTION(BlueprintCallable, Category = "Store|Closing")
	void CompleteDailySummary();

	UFUNCTION(BlueprintCallable, Category = "Store|Closing")
	bool RequestClosingDailySummary();

	// 현재 마감 업무를 완료하고 다음 업무를 활성화합니다.
	UFUNCTION(BlueprintCallable, Category = "Store|Closing")
	void CompleteCurrentEndOfDayTask();

	// 이전 날의 마감 이후 08:00 AM부터 새 영업일을 시작합니다.
	UFUNCTION(BlueprintCallable, Category = "Store|Closing")
	void StartNextBusinessDay();

	// 이 맵에서 튜토리얼을 시작할 건지 여부
	UFUNCTION(BlueprintPure, Category = "Store|Tutorial")
	bool IsTutorialEnabled() const;

	// 튜토리얼 서브시스템이 시작 시점에 이 값을 가져감
	UFUNCTION(BlueprintPure, Category = "Store|Tutorial")
	UDataTable* GetTutorialStepDataTable() const;

	// 튜토리얼 배너/대화창/목표 위젯 클래스 (서브시스템이 시작 시점에 가져감)
	UFUNCTION(BlueprintPure, Category = "Store|Tutorial")
	TSubclassOf<UCSTutorialBannerWidgetBase> GetTutorialBannerWidgetClass() const;

	UFUNCTION(BlueprintPure, Category = "Store|Tutorial")
	TSubclassOf<UCSTutorialDialogueWidgetBase> GetTutorialDialogueWidgetClass() const;

	UFUNCTION(BlueprintPure, Category = "Store|Tutorial")
	TSubclassOf<UCSTutorialObjectiveWidgetBase> GetTutorialObjectiveWidgetClass() const;

	// 튜토리얼 전용: DeliveryArrived 스텝에서 매장 밖에 스폰할 선반 액터 클래스
	// ICSBuildableFacility를 구현하는 클래스여야 함 (그렇지 않으면 스폰은 되지만 경고 로그만 남고 무시됨)
	UFUNCTION(BlueprintPure, Category = "Store|Tutorial")
	TSubclassOf<AActor> GetTutorialShelfFacilityClass() const;

	// 날씨 서브시스템이 시작 시점에 이 값을 가져감
	UFUNCTION(BlueprintPure, Category = "Store|Weather")
	UCSWeatherSettingsDataAsset* GetWeatherSettings() const;

protected:
	virtual void BeginPlay() override;

	// 단계별 시각과 행동 허용 규칙을 읽는 데이터 테이블입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store")
	TObjectPtr<UDataTable> StorePhaseDataTable;

	// 배송, 고객 입장, 피크타임 같은 시간 이벤트를 읽는 데이터 테이블입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store")
	TObjectPtr<UDataTable> StoreScheduleEventDataTable;

	// 상점 레벨과 평판의 초기값 및 임계값을 제공하는 성장 설정입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Progression")
	TObjectPtr<UCSStoreProgressionDataAsset> StoreProgressionDataAsset;

	// 상품 신선도의 초기값과 시간 기반 감소량을 제공하는 전역 설정입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Freshness")
	TObjectPtr<UCSFreshnessSettingsDataAsset> FreshnessSettingsDataAsset;

	// 임대료 납부 주기와 일차별 금액을 제공하는 상점 경제 설정입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Economy")
	TObjectPtr<UCSStoreEconomyDataAsset> StoreEconomyDataAsset;

	// BGM, SFX 이벤트 에셋과 SoundMix 라우팅을 제공하는 공용 오디오 설정입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Audio")
	TObjectPtr<UCSAudioConfigDataAsset> StoreAudioConfigDataAsset;

	// 하루를 시작할 게임 내 시각입니다. 기본값은 08:00 AM입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time", meta = (ClampMin = "0", ClampMax = "1440"))
	float InitialGameTimeMinutes = 480.0f;

	// 실제 1초당 진행할 게임 시간(분)입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time", meta = (ClampMin = "0.01"))
	float GameMinutesPerRealSecond = 1.5f;

	// 다음 날 새벽 02:00에 영업을 강제 종료하고 정산 단계로 전환합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time",
		meta = (ClampMin = "1440.0", ClampMax = "1440.0"))
	float AutomaticClosingTimeMinutes = 1440.0f;

	// 자동 마감 몇 분 전에 일회성 경고를 표시할지 지정합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time",
		meta = (ClampMin = "1.0"))
	float ClosingWarningLeadMinutes = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time",
		meta = (ClampMin = "1.0"))
	float ClosingClockCountdownMinutes = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time")
	float BusinessStartTimeMinutes = 540.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store")
	bool bStartFlowOnBeginPlay = true;

	// 이 값이 true인 게임모드(BP_TutorialGameMode)를 쓰는 맵에서만 튜토리얼이 시작됨
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Tutorial")
	bool bTutorialEnabled = false;

	// 튜토리얼 스텝 목록
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Tutorial")
	TObjectPtr<UDataTable> TutorialStepDataTable;

	// 튜토리얼 위젯 3종
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Tutorial")
	TSubclassOf<UCSTutorialBannerWidgetBase> TutorialBannerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Tutorial")
	TSubclassOf<UCSTutorialDialogueWidgetBase> TutorialDialogueWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Tutorial")
	TSubclassOf<UCSTutorialObjectiveWidgetBase> TutorialObjectiveWidgetClass;

	// 튜토리얼 전용: DeliveryArrived 스텝에서 스폰할 선반 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Tutorial")
	TSubclassOf<AActor> TutorialShelfFacilityClass;

	// 날씨 종류 테이블과 예보 공개 범위를 제공하는 설정입니다.
	// 지정하지 않으면 경고 로그를 남기고 날씨가 항상 맑음으로 유지됩니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Weather")
	TObjectPtr<UCSWeatherSettingsDataAsset> WeatherSettingsDataAsset;

private:
	const FCSStorePhaseData* FindPhaseData(ECStorePhase Phase) const;
	const FCSStorePhaseData* FindPhaseDataAtTime(float GameMinutes) const;
	void UpdateGameClock();
	void UpdateStorePhaseForCurrentTime();
	void CheckScheduleEvents(float PreviousGameMinutes, float NewGameMinutes);
	void BeginClosingCheckpoint();
	FCSStorePhaseData MakePhaseData(ECStorePhase Phase, const FText& DisplayName) const;
	ECEndOfDayTask GetNextEndOfDayTask(ECEndOfDayTask Task) const;

	TObjectPtr<ACSStoreGameState> StoreGameState;
	FTimerHandle GameClockTimerHandle;
	TSet<FName> FiredScheduleEventRows;
	bool bClosingWarningFired = false;
};
