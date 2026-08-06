#pragma once

#include "CoreMinimal.h"
#include "CSStockTypes.generated.h"

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSProductStockInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Stock")
	FName ProductId;

	UPROPERTY(BlueprintReadOnly, Category = "Stock")
	int32 CrateQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stock")
	int32 ShelfQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stock")
	int32 LooseQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Stock")
	int32 TotalQuantity = 0;
};
