#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSStorePricingSubsystem.generated.h"

// 상품 판매가가 실제로 변경(저장)에 성공했을 때만 방송됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnSalePriceChanged,
	FName,
	ProductRowName,
	int32,
	NewSalePrice);

UCLASS()
class CONVENIENCESTORE_API UCSStorePricingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Store|Pricing")
	int32 GetSalePrice(const FDataTableRowHandle& ProductRow) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Pricing")
	bool SetSalePrice(const FDataTableRowHandle& ProductRow, int32 NewSalePrice);

	// 판매가가 성공적으로 변경될 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Store|Pricing")
	FCSOnSalePriceChanged OnSalePriceChanged;

private:
	UPROPERTY(Transient)
	TMap<FName, int32> SalePriceOverrides;
};
