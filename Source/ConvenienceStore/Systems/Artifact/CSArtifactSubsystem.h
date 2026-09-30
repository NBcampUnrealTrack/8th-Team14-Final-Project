#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/CSJournalTypes.h"
#include "CSArtifactSubsystem.generated.h"

class UDataTable;
class ACSPlayerCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSArtifactChanged);

// 보유량과 사용 결과를 플레이 중에 유지하며 도감의 최초 획득 기록과 구분합니다.
UCLASS()
class CONVENIENCESTORE_API UCSArtifactSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void RegisterTable(UDataTable* InTable);
	bool GetDefinition(FName Id, FCSJournalArtifactDataRow& OutRow) const;
	UFUNCTION(BlueprintCallable, Category="Artifact") bool Acquire(FName Id, int32 Quantity = 1);
	UFUNCTION(BlueprintCallable, Category="Artifact") bool TryUse(FName Id, ACSPlayerCharacter* Player, int32 ConfirmedAppliedAmount = -1);
	bool GetStatUseAmounts(FName Id, ACSPlayerCharacter* Player, int32& Requested, int32& Applied) const;
	UFUNCTION(BlueprintPure, Category="Artifact") int32 GetCount(FName Id) const;
	UFUNCTION(BlueprintPure, Category="Artifact") int32 GetAppliedStacks(FName Id) const;
	UFUNCTION(BlueprintPure, Category="Artifact") int32 GetNextAvailableDay(FName Id) const;
	UFUNCTION(BlueprintPure, Category="Artifact", meta=(WorldContext="WorldContextObject"))
	bool IsFairyHousePlaced(const UObject* WorldContextObject) const;
	float GetSpeedMultiplier(ECSArtifactEffectType Effect) const;
	UFUNCTION(BlueprintCallable, Category="Artifact") void ResetArtifacts();
	UPROPERTY(BlueprintAssignable, Category="Artifact") FCSArtifactChanged OnArtifactsChanged;

private:
	bool bUsingArtifact = false;
	// 배치 액터가 다른 테이블을 등록해도 기존 아티팩트의 사용 정보를 유지합니다.
	UPROPERTY(Transient) TArray<TObjectPtr<UDataTable>> ArtifactTables;
	UPROPERTY(Transient) TMap<FName, int32> Counts;
	UPROPERTY(Transient) TMap<FName, int32> AppliedStacks;
	UPROPERTY(Transient) TMap<FName, int32> NextAvailableDays;
};
