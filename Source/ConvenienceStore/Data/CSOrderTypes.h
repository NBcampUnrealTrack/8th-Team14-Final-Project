#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSOrderTypes.generated.h"

UENUM(BlueprintType)
enum class ECSOrderDeliveryType : uint8
{
	NextDay,
	Immediate
};

UENUM(BlueprintType)
enum class ECOrderFailureReason : uint8
{
	None,
	EmptyOrder,
	StoreGameStateUnavailable,
	InvalidProductRow,
	InvalidCrateCount,
	InvalidPurchasePrice,
	InvalidPackQuantity,
	CostOverflow,
	InsufficientFunds,
	DeliveryUnavailable,
	CrateSpawnFailed,
	ProductNotOrderable
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSOrderLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 CrateCount = 0;
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSOrderResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Order")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Order")
	ECOrderFailureReason FailureReason = ECOrderFailureReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "Order")
	int32 TotalCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Order")
	int32 TotalCrateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Order")
	int32 TotalItemQuantity = 0;

	// 정산에서 일반 발주와 퀵 발주 지출을 분리하기 위한 결과값입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Order")
	bool bImmediateDelivery = false;
};
