#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSFreshnessSettingsDataAsset.generated.h"

UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSFreshnessSettingsDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Freshness",
		meta = (ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "100"))
	int32 InitialFreshness = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Freshness",
		meta = (ClampMin = "0.001", UIMin = "0.001"))
	float DecayIntervalSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Freshness",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 DecayAmountPerInterval = 1;
};
