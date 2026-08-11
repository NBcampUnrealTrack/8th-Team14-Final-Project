#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSStorePricingSubsystem.generated.h"

UCLASS()
class CONVENIENCESTORE_API UCSStorePricingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Store|Pricing")
	int32 GetSalePrice(const FDataTableRowHandle& ProductRow) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Pricing")
	bool SetSalePrice(const FDataTableRowHandle& ProductRow, int32 NewSalePrice);

private:
	UPROPERTY(Transient)
	TMap<FName, int32> SalePriceOverrides;
};
