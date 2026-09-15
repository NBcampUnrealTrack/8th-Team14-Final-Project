#pragma once

#include "CoreMinimal.h"
#include "Data/CSEmployeeHiringTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSEmployeeHiringSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnEmployeeHired);

// 업무 배정과 분리된 고용 계약 관리자. 세 자리를 월드 전체에서 공유합니다.
UCLASS()
class CONVENIENCESTORE_API UCSEmployeeHiringSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "Employee|Hiring")
	bool IsSlotHired(ECSEmployeeHireSlot Slot) const;
	UFUNCTION(BlueprintPure, Category = "Employee|Hiring")
	int32 GetTotalDailyWage() const;
	int32 GetWageForBusinessDay(int32 BusinessDay) const;
	bool CanEmployeeWork(const ACSStoreEmployeeCharacter* Employee) const;
	bool CanHire(const ACSStoreEmployeeCharacter* Applicant, FText& OutReason) const;
	bool TryHire(ACSStoreEmployeeCharacter* Applicant, FText& OutReason);
	int32 GetNextStartBusinessDay() const;

	UFUNCTION(BlueprintPure, Category = "Employee|Hiring")
	TArray<FCSEmployeeContract> GetContracts() const { return Contracts; }
	UPROPERTY(BlueprintAssignable, Category = "Employee|Hiring") FCSOnEmployeeHired OnEmployeeHired;
private:
	UPROPERTY(Transient) TArray<FCSEmployeeContract> Contracts;
	bool bHiringInProgress = false;
};
