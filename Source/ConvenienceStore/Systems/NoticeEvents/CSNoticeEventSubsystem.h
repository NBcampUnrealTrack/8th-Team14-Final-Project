#pragma once

#include "CoreMinimal.h"
#include "Data/CSNoticeEventTypes.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSNoticeEventSubsystem.generated.h"

class ACSStoreGameState;
class UDataTable;
class UCSSaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnNoticeEventsChanged);

/** 영업일마다 현재 뉴스와 기념일을 결정하고 데이터 기반 수요 보정값을 제공합니다. */
UCLASS()
class CONVENIENCESTORE_API UCSNoticeEventSubsystem : public UWorldSubsystem,
	public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// ICSSaveParticipant
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	UPROPERTY(BlueprintAssignable, Category = "Notice Events")
	FCSOnNoticeEventsChanged OnNoticeEventsChanged;

	UFUNCTION(BlueprintPure, Category = "Notice Events|News")
	bool HasTodayNews() const;

	UFUNCTION(BlueprintCallable, Category = "Notice Events|News")
	bool GetTodayNews(FCSNewsEventDataRow& OutNews) const;

	/** 오늘 뉴스와 기념일의 활성 수요 효과를 게시판 Row용 배열로 반환합니다. 종족 효과는 빠집니다. */
	UFUNCTION(BlueprintCallable, Category = "Notice Events|News")
	void GetTodayNewsDemandChanges(TArray<FCSNoticeBoardDemandChange>& OutChanges) const;

	// 오늘 뉴스의 RowName만 반환. 뉴스가 없는 날은 NAME_None
	// 행 내용까지는 필요 없고 이름만 필요한 쪽을 위해 둠
	UFUNCTION(BlueprintPure, Category = "Notice Events|News")
	FName GetTodayNewsRowName() const;

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

	// 이 상품의 수요를 가장 크게 끌어올린 출처(뉴스/기념일)를 돌려준다.
	// 올린 출처가 없으면(보정이 0이거나 전부 음수) false - 그때는 OutSource를 건드리지 않는다
	// 속마음을 "뉴스 보고 왔는데" / "기념일이라 왔는데"로 가르려면 합산값만으로는 부족해서 둔다
	bool GetStrongestPositiveDemandSource(
		const FDataTableRowHandle& ProductRow,
		ECSNoticeEventSourceType& OutSource) const;

	// 오늘 뉴스와 기념일이 이 종족 스폰 가중치에 곱할 배수. ArchetypeId는 Archetype 에셋 이름
	float GetRaceSpawnMultiplier(FName ArchetypeId) const;

private:
	UFUNCTION()
	void HandleBusinessDayStarted(int32 NewBusinessDay);

	void LoadEventDataTables();
	void ProcessBusinessDay(int32 BusinessDay);
	void SelectDailyNews();

	// 디버그용 강제 뉴스(cs.news)를 적용. 덮어썼으면 true
	bool ApplyForcedNewsOverride();
	void CollectDailyHolidays(int32 DayOfMonth);

	// 디버그용 강제 기념일(cs.holiday)을 적용. 덮어썼으면 true
	bool ApplyForcedHolidayOverride();
	bool IsEffectSourceActive(const FCSNoticeEventEffectRuleRow& Rule) const;

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
