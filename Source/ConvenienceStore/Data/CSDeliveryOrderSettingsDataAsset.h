#pragma once

#include "CoreMinimal.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "Engine/DataAsset.h"
#include "CSDeliveryOrderSettingsDataAsset.generated.h"

class UDataTable;

/** 단말기 없이 배달 주문을 만들기 위한 상품 후보와 원격 수신처 목록입니다. */
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSDeliveryOrderSettingsDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order|Timing", meta = (ClampMin = "1"))
	float MinimumOrderIntervalMinutes = 45.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order|Timing", meta = (ClampMin = "1"))
	float MaximumOrderIntervalMinutes = 90.0f;
	// 두 번째 주문부터 남은 영업시간을 주문 수만큼 나눈 기준 시각에서 흔들 수 있는 비율입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order|Timing", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float DistributedOrderTimeJitterPercent = 0.2f;
	// 마감 직전 처리할 시간이 없는 주문을 생성하지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order|Timing", meta = (ClampMin = "1"))
	float StopOrdersBeforeClosingMinutes = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	TObjectPtr<UDataTable> ProductDataTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	TArray<FCSDeliveryRecipientInfo> Recipients;
};
