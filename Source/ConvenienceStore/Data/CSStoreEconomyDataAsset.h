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
