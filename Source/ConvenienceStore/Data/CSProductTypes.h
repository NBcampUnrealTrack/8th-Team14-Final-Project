#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSProductTypes.generated.h"

UENUM(BlueprintType)
enum class ECSProductCategory : uint8
{
	Alcohol,
	Beverage,
	FreshFood,
	FrozenFood,
	Potion,
	Scroll,
	Snack,
	Supply,
	Toiletry,
	Tool
};

UENUM(BlueprintType)
enum class ECSProductShelfType : uint8
{
	ChilledShelf,
	FreezerShelf,
	PotionShelf,
	ScrollRack,
	SnackShelf,
	ToolRack,
	UtilityShelf
};

UENUM(BlueprintType)
enum class ECSProductRarity : uint8
{
	Common,
	Uncommon,
	Rare
};

UENUM(BlueprintType)
enum class ECSReputationGrade : uint8
{
	None = 0 UMETA(Hidden),
	VeryBad = 1,
	Bad = 2,
	Normal = 3,
	Good = 4,
	Excellent = 5
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSProductDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	ECSProductCategory Category = ECSProductCategory::Beverage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Price",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 PurchasePrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Price",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 RecommendedSalePrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Order",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 OrderPackQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|ShelfLife",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 ShelfLifeDays = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|ShelfLife")
	bool bPerishable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Shelf")
	ECSProductShelfType ShelfType = ECSProductShelfType::ChilledShelf;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Demand",
		meta = (ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "100"))
	int32 BaseDemand = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	ECSProductRarity Rarity = ECSProductRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Unlock",
		meta = (ClampMin = "1", ClampMax = "10", UIMin = "1", UIMax = "10"))
	int32 StoreLevelUnlockCondition = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Unlock")
	ECSReputationGrade ReputationUnlockCondition = ECSReputationGrade::VeryBad;
};
