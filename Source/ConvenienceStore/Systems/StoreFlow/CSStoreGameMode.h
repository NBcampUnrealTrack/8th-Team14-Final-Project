#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSStoreGameMode.generated.h"

class ACSStoreGameState;
class UDataTable;

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

	// 현재 마감 업무를 완료하고 다음 업무를 활성화합니다.
	UFUNCTION(BlueprintCallable, Category = "Store|Closing")
	void CompleteCurrentEndOfDayTask();

	// 이전 날의 마감 이후 08:00 AM부터 새 영업일을 시작합니다.
	UFUNCTION(BlueprintCallable, Category = "Store|Closing")
	void StartNextBusinessDay();

protected:
	virtual void BeginPlay() override;

	// 단계별 시각과 행동 허용 규칙을 읽는 데이터 테이블입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store")
	TObjectPtr<UDataTable> StorePhaseDataTable;

	// 배송, 고객 입장, 피크타임 같은 시간 이벤트를 읽는 데이터 테이블입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store")
	TObjectPtr<UDataTable> StoreScheduleEventDataTable;

	// 하루를 시작할 게임 내 시각입니다. 기본값은 08:00 AM입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time", meta = (ClampMin = "0", ClampMax = "1440"))
	float InitialGameTimeMinutes = 480.0f;

	// 실제 1초당 진행할 게임 시간(분)입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Time", meta = (ClampMin = "0.01"))
	float GameMinutesPerRealSecond = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store")
	bool bStartFlowOnBeginPlay = true;

private:
	const FCSStorePhaseData* FindPhaseData(ECStorePhase Phase) const;
	const FCSStorePhaseData* FindPhaseDataAtTime(float GameMinutes) const;
	void UpdateGameClock();
	void UpdateStorePhaseForCurrentTime();
	void CheckScheduleEvents(float PreviousGameMinutes, float NewGameMinutes);
	void BeginClosingCheckpoint();
	ECEndOfDayTask GetNextEndOfDayTask(ECEndOfDayTask Task) const;

	TObjectPtr<ACSStoreGameState> StoreGameState;
	FTimerHandle GameClockTimerHandle;
	TSet<FName> FiredScheduleEventRows;
};
