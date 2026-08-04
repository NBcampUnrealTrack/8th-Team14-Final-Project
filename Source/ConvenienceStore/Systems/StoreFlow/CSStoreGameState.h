#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSStoreGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStorePhaseChanged,
	ECStorePhase,
	NewPhase);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStoreRemainingTimeChanged,
	float,
	NewRemainingSeconds);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStoreGameTimeChanged,
	float,
	NewGameMinutes);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnEndOfDayTaskChanged,
	ECEndOfDayTask,
	NewTask);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnStoreScheduleEventFired,
	FName,
	EventId,
	FText,
	Message);

// 게임플레이 액터와 UI가 읽는 편의점의 현재 상태를 보관합니다.
UCLASS()
class CONVENIENCESTORE_API ACSStoreGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	// 현재 진행 중인 편의점 단계입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store")
	ECStorePhase CurrentPhase = ECStorePhase::Preparation;

	// HUD에 표시할 현재 단계의 이름입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store")
	FText CurrentPhaseDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Store")
	float RemainingSeconds = 0.0f;

	// 12:00 AM부터의 경과 분이며, 자정에는 1440이 될 수 있습니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Time")
	float CurrentGameMinutes = 480.0f;

	// 실제 1초 동안 흘러가는 게임 시간(분)입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Time")
	float GameMinutesPerRealSecond = 1.5f;

	// 시간이 진행 중인지 나타냅니다. 마감 업무 중에는 거짓입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Time")
	bool bIsGameClockRunning = false;

	// 자정 이후 플레이어가 처리해야 하는 현재 마감 업무입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Closing")
	ECEndOfDayTask CurrentEndOfDayTask = ECEndOfDayTask::None;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Rules")
	bool bCanRestock = false;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Rules")
	bool bCanCheckout = false;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Rules")
	bool bCanSpawnCustomers = false;

	// 단계가 변경되었을 때 UI와 게임플레이에 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Store")
	FOnStorePhaseChanged OnStorePhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Store")
	FOnStoreRemainingTimeChanged OnRemainingTimeChanged;

	// 게임 시각이 변경되었을 때 시계와 조명에 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Store|Time")
	FOnStoreGameTimeChanged OnGameTimeChanged;

	// 마감 업무가 변경되었을 때 마감 UI에 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Store|Closing")
	FOnEndOfDayTaskChanged OnEndOfDayTaskChanged;

	UPROPERTY(BlueprintAssignable, Category = "Store|Schedule")
	FOnStoreScheduleEventFired OnScheduleEventFired;

	void SetStorePhase(const FCSStorePhaseData& PhaseData);
	void SetRemainingSeconds(float NewRemainingSeconds);
	void SetGameClock(float NewGameMinutes, float NewGameMinutesPerRealSecond, bool bNewClockRunning);
	void SetCurrentGameMinutes(float NewGameMinutes);
	void SetEndOfDayTask(ECEndOfDayTask NewTask);
};
