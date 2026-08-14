#include "Systems/Inventory/CSFacilityInventorySubsystem.h"

#include "EngineUtils.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "GameFramework/Actor.h"

int32 UCSFacilityInventorySubsystem::GetOwnedFacilityCount(FName FacilityId) const
{
	if (FacilityId.IsNone() || GetWorld() == nullptr)
	{
		return 0;
	}

	int32 OwnedCount = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* FacilityActor = *It;
		if (!IsValid(FacilityActor) ||
			!FacilityActor->Implements<UCSBuildableFacility>() ||
			ICSBuildableFacility::Execute_GetFacilityId(FacilityActor) != FacilityId)
		{
			continue;
		}

		if (OwnedCount < MAX_int32)
		{
			++OwnedCount;
		}
	}

	return OwnedCount;
}

TMap<FName, int32> UCSFacilityInventorySubsystem::GetAllOwnedFacilityCounts() const
{
	TMap<FName, int32> OwnedCounts;
	if (GetWorld() == nullptr)
	{
		return OwnedCounts;
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* FacilityActor = *It;
		if (!IsValid(FacilityActor) ||
			!FacilityActor->Implements<UCSBuildableFacility>())
		{
			continue;
		}

		const FName FacilityId =
			ICSBuildableFacility::Execute_GetFacilityId(FacilityActor);
		if (FacilityId.IsNone())
		{
			continue;
		}

		int32& OwnedCount = OwnedCounts.FindOrAdd(FacilityId);
		if (OwnedCount < MAX_int32)
		{
			++OwnedCount;
		}
	}

	return OwnedCounts;
}
