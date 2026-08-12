#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Data/CSCheckoutTypes.h"
#include "Data/CSProductTypes.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSStoreGameState.generated.h"

class UCSStoreProgressionDataAsset;

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStoreCheckoutRecorded,
	const FCSCheckoutTransaction&,
	Transaction);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStoreDailySalesChanged);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStoreFundsChanged,
	int32,
	NewFunds);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStoreBusinessDayStarted,
	int32,
	NewBusinessDay);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnStoreExperienceChanged,
	int32,
	NewExperience);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnStoreLevelChanged,
	int32,
	NewLevel);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnReputationChanged,
	int32,
	NewReputationScore);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnReputationGradeChanged,
	ECSReputationGrade,
	NewReputationGrade);

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

	// 현재 진행 중인 영업일입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Sales")
	int32 CurrentBusinessDay = 1;

	// 오늘 결제 완료된 거래의 총매출입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Sales")
	int32 DailySalesGold = 0;

	// 오늘 판매된 상품의 총매입가입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Sales")
	int32 DailyCostGold = 0;

	// 오늘 총매출에서 총매입가를 뺀 이익입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Sales")
	int32 DailyProfitGold = 0;

	// 오늘 결제 완료된 상품의 총수량입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Sales")
	int32 DailySoldQuantity = 0;

	// 오늘 완료한 손님 결제 횟수입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Sales")
	int32 CompletedCheckoutCount = 0;

	// 오늘 발생한 결제 거래를 정산과 판매 보고서에서 조회할 수 있도록 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Sales")
	TArray<FCSCheckoutTransaction> DailyCheckoutTransactions;

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

	// 결제 거래가 정산 데이터에 기록된 직후 거래 상세를 방송합니다.
	UPROPERTY(BlueprintAssignable, Category = "Store|Sales")
	FOnStoreCheckoutRecorded OnCheckoutRecorded;

	// 일일 매출 요약값이 변경되었을 때 HUD와 정산 UI에 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Store|Sales")
	FOnStoreDailySalesChanged OnDailySalesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Store|Funds")
	FOnStoreFundsChanged OnFundsChanged;

	// 다음 영업일이 시작되어 상품의 유통기한과 보관 상태를 갱신할 때 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Store|Time")
	FOnStoreBusinessDayStarted OnBusinessDayStarted;

	UPROPERTY(BlueprintAssignable, Category = "Store|Progression")
	FCSOnStoreExperienceChanged OnStoreExperienceChanged;

	UPROPERTY(BlueprintAssignable, Category = "Store|Progression")
	FCSOnStoreLevelChanged OnStoreLevelChanged;

	UPROPERTY(BlueprintAssignable, Category = "Store|Progression")
	FCSOnReputationChanged OnReputationChanged;

	UPROPERTY(BlueprintAssignable, Category = "Store|Progression")
	FCSOnReputationGradeChanged OnReputationGradeChanged;

	void SetStorePhase(const FCSStorePhaseData& PhaseData);
	void SetRemainingSeconds(float NewRemainingSeconds);
	void SetGameClock(float NewGameMinutes, float NewGameMinutesPerRealSecond, bool bNewClockRunning);
	void SetCurrentGameMinutes(float NewGameMinutes);
	void SetEndOfDayTask(ECEndOfDayTask NewTask);

	UFUNCTION(BlueprintPure, Category = "Store|Funds")
	int32 GetCurrentFunds() const;

	UFUNCTION(BlueprintPure, Category = "Store|Funds")
	bool CanAfford(int32 Amount) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Funds")
	bool TrySpendFunds(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Store|Funds")
	bool AddFunds(int32 Amount);

	bool InitializeStoreProgression(UCSStoreProgressionDataAsset* InProgressionDataAsset);

	UFUNCTION(BlueprintPure, Category = "Store|Progression")
	int32 GetStoreExperience() const;

	UFUNCTION(BlueprintPure, Category = "Store|Progression")
	int32 GetStoreLevel() const;

	UFUNCTION(BlueprintPure, Category = "Store|Progression")
	int32 GetReputationScore() const;

	UFUNCTION(BlueprintPure, Category = "Store|Progression")
	ECSReputationGrade GetReputationGrade() const;

	UFUNCTION(BlueprintCallable, Category = "Store|Progression")
	bool AddStoreExperience(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Store|Progression")
	bool AddReputation(int32 Delta);

	UFUNCTION(BlueprintPure, Category = "Store|Progression")
	bool IsStoreLevelRequirementMet(int32 RequiredLevel) const;

	UFUNCTION(BlueprintPure, Category = "Store|Progression")
	bool IsReputationRequirementMet(ECSReputationGrade RequiredGrade) const;

	// 스캔이 끝난 주문을 거래로 기록하고 오늘의 매출·원가·이익을 갱신합니다.
	bool RecordCheckoutTransaction(const TArray<FCSCheckoutLineItem>& LineItems);

	// 다음 영업일을 시작할 때 하루 단위 판매 기록과 요약값을 초기화합니다.
	void BeginNextBusinessDay();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Funds",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 StartingFunds = 1000;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Funds")
	int32 CurrentFunds = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Progression")
	int32 CurrentStoreExperience = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Progression")
	int32 CurrentStoreLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Progression")
	int32 CurrentReputationScore = 6000;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Progression")
	ECSReputationGrade CurrentReputationGrade = ECSReputationGrade::Normal;

	UPROPERTY(Transient)
	TObjectPtr<UCSStoreProgressionDataAsset> StoreProgressionDataAsset;

private:
	// 영업일 안에서 다음 거래에 사용할 순번입니다.
	int32 NextTransactionId = 1;
};
