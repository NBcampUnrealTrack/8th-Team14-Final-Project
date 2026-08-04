#include "Features/Shelves/CSShelfActor.h"

#include "Components/StaticMeshComponent.h"

ACSShelfActor::ACSShelfActor()
{
	PrimaryActorTick.bCanEverTick = false;

	ShelfMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShelfMesh"));
	SetRootComponent(ShelfMesh);
	ShelfMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

bool ACSShelfActor::CanPlaceItem_Implementation(AActor* ItemActor) const
{
	return IsValid(ItemActor) &&
		Slots.ContainsByPredicate([](const FCSShelfSlot& Slot)
		{
			return !IsValid(Slot.OccupyingItem);
		});
}

int32 ACSShelfActor::FindClosestEmptySlot(FVector PlacementHint) const
{
	int32 ClosestSlotIndex = INDEX_NONE;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const FCSShelfSlot& Slot = Slots[SlotIndex];
		if (IsValid(Slot.OccupyingItem))
		{
			continue;
		}

		const FTransform SlotWorldTransform =
			Slot.LocalTransform * GetActorTransform();
		const float DistanceSquared = FVector::DistSquared(
			SlotWorldTransform.GetLocation(),
			PlacementHint);

		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestSlotIndex = SlotIndex;
		}
	}

	return ClosestSlotIndex;
}

bool ACSShelfActor::GetPlacementPreview_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	const int32 SlotIndex = FindClosestEmptySlot(PlacementHint);
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	OutSlotWorldTransform =
		Slots[SlotIndex].LocalTransform * GetActorTransform();
	return true;
}

bool ACSShelfActor::TryPlaceItem_Implementation(
	AActor* ItemActor,
	FVector PlacementHint)
{
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	const int32 SlotIndex = FindClosestEmptySlot(PlacementHint);
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	FCSShelfSlot& SelectedSlot = Slots[SlotIndex];
	const FTransform SlotWorldTransform =
		SelectedSlot.LocalTransform * GetActorTransform();

	ItemActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ItemActor->SetActorLocationAndRotation(
		SlotWorldTransform.GetLocation(),
		SlotWorldTransform.GetRotation());
	ItemActor->AttachToActor(
		this,
		FAttachmentTransformRules::KeepWorldTransform);
	ItemActor->SetActorEnableCollision(true);

	SelectedSlot.OccupyingItem = ItemActor;
	return true;
}

void ACSShelfActor::RemovePlacedItem_Implementation(AActor* ItemActor)
{
	for (FCSShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem == ItemActor)
		{
			Slot.OccupyingItem = nullptr;
			return;
		}
	}
}
