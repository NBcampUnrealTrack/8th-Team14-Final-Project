#pragma once

#include "CoreMinimal.h"
#include "Data/CSOrderTypes.h"
#include "Data/CSFacilityOrderTypes.h"
#include "CSCombinedOrderTypes.generated.h"

UENUM(BlueprintType)
enum class ECSCombinedOrderFailure : uint8
{
	None, EmptyCart, Unavailable, ProductInvalid, FacilityInvalid,
	ProductLocked, InsufficientFunds, CostOverflow, DeliveryUnavailable, SpawnFailed, Busy
};

/** Quote success validates items and funds; delivery configuration is checked on submission. */
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCombinedOrderResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly) ECSCombinedOrderFailure Failure = ECSCombinedOrderFailure::None;
	UPROPERTY(BlueprintReadOnly) FText Message;
	UPROPERTY(BlueprintReadOnly) FCSOrderResult Products;
	UPROPERTY(BlueprintReadOnly) FCSFacilityOrderResult Facilities;
	UPROPERTY(BlueprintReadOnly) int32 TotalCost = 0;
	UPROPERTY(BlueprintReadOnly) int32 CurrentFunds = 0;
	/** Quote: projected balance. Successful receipt: actual balance after payment. */
	UPROPERTY(BlueprintReadOnly) int32 RemainingFunds = 0;
	UPROPERTY(BlueprintReadOnly) ECSOrderDeliveryType DeliveryType = ECSOrderDeliveryType::NextDay;
};
