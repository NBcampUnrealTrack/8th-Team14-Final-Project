#pragma once

#include "CoreMinimal.h"
#include "Data/CSStockTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSStoreStockSubsystem.generated.h"

UCLASS()
class CONVENIENCESTORE_API UCSStoreStockSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Store|Stock")
	FCSProductStockInfo GetProductStock(FName ProductId) const;

	UFUNCTION(BlueprintPure, Category = "Store|Stock")
	TArray<FCSProductStockInfo> GetAllProductStock() const;
};
