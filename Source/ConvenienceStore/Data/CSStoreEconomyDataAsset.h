#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSStoreEconomyDataAsset.generated.h"

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSRentTier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Rent",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 StartBusinessDay = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Rent",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 RentGold = 0;
};

// 시작 소지금, 납부 주기와 일차별 임대료 구간을 에디터에서 관리하는 상점 경제 설정입니다.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSStoreEconomyDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// 배열 0~3은 각각 2~5단계로 강화하는 비용입니다.
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadOnly, Category = "Store|Economy|MagicCircle", meta = (ClampMin = "1"))
	TArray<int32> MagicCircleUpgradeCosts = { 10000, 30000, 50000, 100000 };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Employees", meta = (ClampMin = "1", ClampMax = "100000000"))
	int32 FirstRestockHireCost = 1000;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Employees", meta = (ClampMin = "1", ClampMax = "100000000"))
	int32 CheckoutHireCost = 2500;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Employees", meta = (ClampMin = "1", ClampMax = "100000000"))
	int32 SecondRestockHireCost = 5000;
	// 소수 급여는 G 단위로 올림합니다. 계약 시 확정하므로 이후 가격 변경에 영향받지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Employees", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float EmployeeDailyWagePercent = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Funds",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 InitialFunds = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Rent",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 FirstRentBusinessDay = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Rent",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 RentPaymentIntervalDays = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Economy|Rent")
	TArray<FCSRentTier> RentTiers;

	UFUNCTION(BlueprintPure, Category = "Store|Economy|Rent")
	bool IsRentPaymentDay(int32 BusinessDay) const;

	UFUNCTION(BlueprintPure, Category = "Store|Economy|Rent")
	int32 GetRentGoldForBusinessDay(int32 BusinessDay) const;

	bool IsConfigurationValid() const;
};
