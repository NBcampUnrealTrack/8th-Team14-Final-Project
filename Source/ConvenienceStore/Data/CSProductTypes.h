#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSProductTypes.generated.h"

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSProductDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Price",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 PurchasePrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Price",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 RecommendedSalePrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Order",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 OrderPackQuantity = 1;
};
