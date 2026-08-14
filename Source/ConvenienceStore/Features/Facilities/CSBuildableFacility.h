#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSBuildableFacility.generated.h"

UINTERFACE(BlueprintType)
class CONVENIENCESTORE_API UCSBuildableFacility : public UInterface
{
	GENERATED_BODY()
};

class CONVENIENCESTORE_API ICSBuildableFacility
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Facility")
	FName GetFacilityId() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Facility|Build")
	bool IsFacilityInstalled() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Facility|Build")
	void InitializeAsUninstalledFacility();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Facility|Build")
	void PrepareForBuildPickup();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Facility|Build")
	void FinalizeBuildPlacement();
};
