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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	TObjectPtr<UDataTable> ProductDataTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	TArray<FCSDeliveryRecipientInfo> Recipients;
};
