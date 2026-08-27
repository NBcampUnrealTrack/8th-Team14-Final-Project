#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSFacilityOrderTypes.generated.h"

UENUM(BlueprintType)
enum class ECFacilityOrderFailureReason : uint8
{
	None,
	EmptyOrder,
	StoreGameStateUnavailable,
	InvalidFacilityRow,
	InvalidQuantity,
	InvalidPurchasePrice,
	InvalidStoreLevelCondition,
	StoreLevelLocked,
	ReputationLocked,
	QuestRequirementUnsupported,
	CostOverflow,
	InsufficientFunds,
	DeliveryUnavailable,
	FacilityClassUnavailable,
	FacilitySpawnFailed,
	SpawnedFacilityInvalid,
	FacilityIdMismatch,
	FacilityInitializationFailed
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSFacilityOrderLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order|Facility",
		meta = (RowType = "/Script/ConvenienceStore.CSFacilityDataRow"))
	FDataTableRowHandle FacilityRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order|Facility",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 Quantity = 0;
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSFacilityOrderResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Order|Facility")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Order|Facility")
	ECFacilityOrderFailureReason FailureReason = ECFacilityOrderFailureReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "Order|Facility")
	int32 TotalCost = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Order|Facility")
	int32 TotalFacilityCount = 0;

	// 정산에서 일반 발주와 퀵 발주 지출을 분리하기 위한 결과값입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Order|Facility")
	bool bImmediateDelivery = false;
};
