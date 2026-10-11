#pragma once

#include "CoreMinimal.h"
#include "Data/CSEmployeeHiringTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSEmployeeHiringSubsystem.generated.h"

class UCSSaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnEmployeeHired);

// 업무 배정과 분리된 고용 계약 관리자. 세 자리를 월드 전체에서 공유합니다.
UCLASS()
class CONVENIENCESTORE_API UCSEmployeeHiringSubsystem : public UWorldSubsystem,
	public ICSSaveParticipant
{
	GENERATED_BODY()
public:
	// ICSSaveParticipant
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	// 세이브 복원 전용으로 고용비 청구 없이 계약을 복원하고 직원 액터를 연결
	// TryHire()는 결제를 포함하므로 로드에 재사용하면 고용비를 이중 청구
	// 고용소가 BeginPlay에서 생성한 지원자를 고용 자리 기준으로 계약에 연결
	void RestoreContracts(const TArray<FCSEmployeeContract>& InContracts);

	UFUNCTION(BlueprintPure, Category = "Employee|Hiring")
	bool IsSlotHired(ECSEmployeeHireSlot Slot) const;
	UFUNCTION(BlueprintPure, Category = "Employee|Hiring")
	int32 GetTotalDailyWage() const;
	int32 GetWageForBusinessDay(int32 BusinessDay) const;
	bool CanEmployeeWork(const ACSStoreEmployeeCharacter* Employee) const;
	bool CanHire(const ACSStoreEmployeeCharacter* Applicant, FText& OutReason) const;
	bool TryHire(ACSStoreEmployeeCharacter* Applicant, FText& OutReason);
	const FCSEmployeeContract* FindContract(const ACSStoreEmployeeCharacter* Employee) const;
	// 그날 일급은 마감 때 이미 나가므로 해고는 영업이 끝난 뒤 잠들기 전에만 받는다
	bool CanDismiss(const ACSStoreEmployeeCharacter* Employee, FText& OutReason) const;
	bool TryDismiss(ACSStoreEmployeeCharacter* Employee, FText& OutReason);
	int32 GetNextStartBusinessDay() const;

	UFUNCTION(BlueprintPure, Category = "Employee|Hiring")
	TArray<FCSEmployeeContract> GetContracts() const { return Contracts; }
	UPROPERTY(BlueprintAssignable, Category = "Employee|Hiring") FCSOnEmployeeHired OnEmployeeHired;
private:
	UPROPERTY(Transient) TArray<FCSEmployeeContract> Contracts;
	bool bHiringInProgress = false;
};
