#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSStorePhaseTypes.generated.h"

UENUM(BlueprintType)
enum class ECStorePhase : uint8 { Preparation, Open, Settlement, Finished };

UENUM(BlueprintType)
enum class ECEndOfDayTask : uint8 { None, OrderReview, Settlement, SalesReport, ReputationUpdate };

UENUM(BlueprintType)
enum class ECStoreScheduleEventType : uint8 { DeliveryArrival, CustomerEntryStart, PeakStart, PeakEnd, Custom };

// DT_StorePhase의 한 행으로, 하루 중 특정 시간 구간의 규칙을 정의합니다.
USTRUCT(BlueprintType)
struct FCSStorePhaseData : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") ECStorePhase Phase = ECStorePhase::Preparation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "0", ClampMax = "1440")) int32 StartTimeMinutes = 480;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "0", ClampMax = "1440")) int32 EndTimeMinutes = 540;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules") bool bCanRestock = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules") bool bCanCheckout = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules") bool bCanSpawnCustomers = false;
};

// DT_StoreScheduleEvent의 한 행으로, 지정 시각에 하루 한 번 발생합니다.
USTRUCT(BlueprintType)
struct FCSStoreScheduleEventData : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule", meta = (ClampMin = "0", ClampMax = "1440")) int32 TriggerTimeMinutes = 480;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") ECStoreScheduleEventType EventType = ECStoreScheduleEventType::Custom;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") FText Message;
};
