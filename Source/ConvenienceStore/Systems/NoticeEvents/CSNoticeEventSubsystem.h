#pragma once

#include "CoreMinimal.h"
#include "Data/CSNoticeEventTypes.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSNoticeEventSubsystem.generated.h"

class ACSStoreGameState;
class UDataTable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnNoticeEventsChanged);

/** 영업일마다 현재 뉴스와 기념일을 결정하고 데이터 기반 수요 보정값을 제공합니다. */
UCLASS()
class CONVENIENCESTORE_API UCSNoticeEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "Notice Events")
	FCSOnNoticeEventsChanged OnNoticeEventsChanged;

	UFUNCTION(BlueprintPure, Category = "Notice Events|News")
	bool HasTodayNews() const;

	UFUNCTION(BlueprintCallable, Category = "Notice Events|News")
	bool GetTodayNews(FCSNewsEventDataRow& OutNews) const;

	UFUNCTION(BlueprintPure, Category = "Notice Events|Holiday")
	bool HasTodayHoliday() const;

	UFUNCTION(BlueprintCallable, Category = "Notice Events|Holiday")
	bool GetTodayHoliday(FCSHolidayEventDataRow& OutHoliday) const;

	UFUNCTION(BlueprintCallable, Category = "Notice Events|Holiday")
	void GetTodayHolidays(TArray<FCSHolidayEventDataRow>& OutHolidays) const;

	UFUNCTION(BlueprintPure, Category = "Notice Events|Demand")
	float GetProductDemandModifier(const FDataTableRowHandle& ProductRow) const;

	UFUNCTION(BlueprintPure, Category = "Notice Events|Demand")
	float GetProductDemandMultiplier(const FDataTableRowHandle& ProductRow) const;

private:
	UFUNCTION()
	void HandleBusinessDayStarted(int32 NewBusinessDay);

	void LoadEventDataTables();
	void ProcessBusinessDay(int32 BusinessDay);
	void SelectDailyNews();
	void CollectDailyHolidays(int32 DayOfMonth);
	bool IsEffectSourceActive(const FCSNoticeEventEffectRuleRow& Rule) const;

	// 고정 경로의 soft reference를 CDO에 보관해 수동 연결 없이 로드하고 cook 의존성도 표현합니다.
	UPROPERTY()
	TSoftObjectPtr<UDataTable> NewsEventDataTableAsset = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(TEXT("/Game/ConvenienceStore/Data/NoticeEvents/DT_NewsEvent.DT_NewsEvent")));

	UPROPERTY()
	TSoftObjectPtr<UDataTable> HolidayEventDataTableAsset = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(TEXT("/Game/ConvenienceStore/Data/NoticeEvents/DT_HolidayEvent.DT_HolidayEvent")));

	UPROPERTY()
	TSoftObjectPtr<UDataTable> EffectRuleDataTableAsset = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(TEXT("/Game/ConvenienceStore/Data/NoticeEvents/DT_NoticeEventEffectRule.DT_NoticeEventEffectRule")));

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> NewsEventDataTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> HolidayEventDataTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> EffectRuleDataTable;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> SubscribedStoreGameState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Notice Events|News",
		meta = (AllowPrivateAccess = "true"))
	FName CurrentNewsRowName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Notice Events|Holiday",
		meta = (AllowPrivateAccess = "true"))
	TArray<FName> ActiveHolidayRowNames;

	int32 LastProcessedBusinessDay = INDEX_NONE;
	FRandomStream NoticeEventRandomStream;
};
