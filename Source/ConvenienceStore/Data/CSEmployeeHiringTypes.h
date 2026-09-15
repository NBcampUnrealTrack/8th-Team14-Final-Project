#pragma once

#include "CoreMinimal.h"
#include "CSEmployeeHiringTypes.generated.h"

class ACSStoreEmployeeCharacter;

// 가격과 최대 인원을 결정하는 고정 자리입니다. 고용 순서에는 제한이 없습니다.
UENUM(BlueprintType)
enum class ECSEmployeeHireSlot : uint8
{
	FirstRestock UMETA(DisplayName = "첫 번째 진열 직원"),
	Checkout UMETA(DisplayName = "계산 직원"),
	SecondRestock UMETA(DisplayName = "두 번째 진열 직원"),
};

USTRUCT(BlueprintType)
struct FCSEmployeeContract
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ECSEmployeeHireSlot Slot = ECSEmployeeHireSlot::FirstRestock;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 HireCost = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 DailyWage = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 StartBusinessDay = 1;
	// 액터가 제거되어도 계약과 인건비는 유지됩니다.
	UPROPERTY(Transient) TWeakObjectPtr<ACSStoreEmployeeCharacter> Employee;
};
