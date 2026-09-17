#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/CSJournalTypes.h"
#include "CSJournalSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSJournalChanged);

// 플레이 중 수첩 기록을 유지하며 별도 파일에는 저장하지 않습니다. 향후 세이브·로드 시스템에서 기록을 저장하고 복원하실 수 있습니다.
UCLASS()
class CONVENIENCESTORE_API UCSJournalSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
  public:
	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	// 새 게임 시작 시 해금 상태와 누적 기록을 초기화하며 카탈로그 설정은 유지합니다.
	UFUNCTION(BlueprintCallable, Category = "Journal") void ResetJournal();
	UFUNCTION(BlueprintCallable, Category = "Journal") bool UnlockEntry(FName EntryId);
	UFUNCTION(BlueprintCallable, Category = "Journal") bool CollectArtifact(FName ArtifactId);
	UFUNCTION(BlueprintCallable, Category = "Journal")
	void RecordEvent(FName EventId, int32 BusinessDay, FText Title, FText Body);
	UFUNCTION(BlueprintCallable, Category = "Journal") void MarkDayRead(int32 BusinessDay);
	UFUNCTION(BlueprintPure, Category = "Journal") bool IsEntryUnlocked(FName Id) const
	{
		return UnlockedEntries.Contains(Id);
	}
	UFUNCTION(BlueprintPure, Category = "Journal") bool IsArtifactCollected(FName Id) const
	{
		return CollectedArtifacts.Contains(Id);
	}
	UFUNCTION(BlueprintPure, Category = "Journal") TArray<FCSJournalEvent> GetEvents() const
	{
		return Events;
	}
	UFUNCTION(BlueprintPure, Category = "Journal") TArray<FCSJournalDelivery> GetDeliveryHistory() const
	{
		return Deliveries;
	}
	void ObserveDay(int32 Day);
	void RecordDelivery(const FCSDeliveryOrderRuntime &Order, int32 Day, FName LevelId);
	UPROPERTY(BlueprintAssignable) FCSJournalChanged OnJournalChanged;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Journal") TObjectPtr<UCSJournalCatalog> Catalog;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Journal") TSet<FName> UnlockedEntries;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Journal") TSet<FName> CollectedArtifacts;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Journal") TArray<FCSJournalEvent> Events;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Journal") TArray<FCSJournalDelivery> Deliveries;
	// 현재까지 도달한 가장 늦은 영업일이며 열람할 수 있는 마지막 달을 결정합니다.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Journal") int32 LatestDay = 1;
};
