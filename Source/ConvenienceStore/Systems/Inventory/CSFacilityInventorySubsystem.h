#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSFacilityInventorySubsystem.generated.h"

UCLASS()
class CONVENIENCESTORE_API UCSFacilityInventorySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Store|Facility")
	int32 GetOwnedFacilityCount(FName FacilityId) const;

	UFUNCTION(BlueprintPure, Category = "Store|Facility")
	TMap<FName, int32> GetAllOwnedFacilityCounts() const;
};
