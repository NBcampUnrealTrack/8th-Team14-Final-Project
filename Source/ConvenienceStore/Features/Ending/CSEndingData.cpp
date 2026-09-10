#include "Features/Ending/CSEndingData.h"
#include "Features/Ending/CSEndingActors.h"

UCSEndingData::UCSEndingData()
{
	const FName Ids[] = { TEXT("Compass"), TEXT("ResonanceStone"), TEXT("BoundaryCore"), TEXT("ReturnCore") };
	const FText Names[] = {
		NSLOCTEXT("Ending", "Compass", "현세의 나침반"),
		NSLOCTEXT("Ending", "ResonanceStone", "차원 공명석"),
		NSLOCTEXT("Ending", "BoundaryCore", "경계 고정핵"),
		NSLOCTEXT("Ending", "ReturnCore", "귀환의 문핵") };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FCSEndingItemDefinition& Item = Items.AddDefaulted_GetRef();
		Item.ItemId = Ids[Index];
		Item.DisplayName = Names[Index];
		Item.ActorClass = ACSEndingItemActor::StaticClass();
	}
}

bool UCSEndingData::IsConfigurationValid() const
{
	if (Items.Num() != 4 || ActivationGold < 0) return false;
	TSet<FName> Ids;
	for (const FCSEndingItemDefinition& Item : Items)
	{
		if (Item.ItemId.IsNone() || Ids.Contains(Item.ItemId) || Item.PriceGold < 0 || !Item.ActorClass ||
			Item.ActorClass->HasAnyClassFlags(CLASS_Abstract)) return false;
		Ids.Add(Item.ItemId);
	}
	return true;
}
