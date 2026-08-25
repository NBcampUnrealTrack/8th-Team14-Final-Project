#include "Features/Inventory/CSQuickbarInventoryComponent.h"

#include "Features/Tools/CSUsableToolActor.h"

UCSQuickbarInventoryComponent::UCSQuickbarInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCSQuickbarInventoryComponent::InitializeQuickbar()
{
	const int32 RequestedSlotCount = FMath::Max(1, InventorySlotCount);
	if (Slots.Num() != RequestedSlotCount)
	{
		Slots.SetNum(RequestedSlotCount);
	}
	SelectedSlotIndex = FMath::Clamp(SelectedSlotIndex, 0, Slots.Num() - 1);
	OnQuickbarChanged.Broadcast();
	OnQuickbarSelectionChanged.Broadcast(SelectedSlotIndex);
}

int32 UCSQuickbarInventoryComponent::TryAddTool(ACSUsableToolActor* ToolActor)
{
	if (!IsValid(ToolActor))
	{
		return INDEX_NONE;
	}

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		if (Slots[SlotIndex].ToolActor == ToolActor)
		{
			return SlotIndex;
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		if (!IsValid(Slots[SlotIndex].ToolActor))
		{
			Slots[SlotIndex].ToolActor = ToolActor;
			OnQuickbarChanged.Broadcast();
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

bool UCSQuickbarInventoryComponent::RemoveTool(ACSUsableToolActor* ToolActor)
{
	for (FCSQuickbarSlot& Slot : Slots)
	{
		if (Slot.ToolActor == ToolActor)
		{
			Slot.ToolActor = nullptr;
			OnQuickbarChanged.Broadcast();
			return true;
		}
	}
	return false;
}

bool UCSQuickbarInventoryComponent::SelectSlot(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex) || SelectedSlotIndex == SlotIndex)
	{
		return Slots.IsValidIndex(SlotIndex);
	}

	SelectedSlotIndex = SlotIndex;
	OnQuickbarSelectionChanged.Broadcast(SelectedSlotIndex);
	return true;
}

bool UCSQuickbarInventoryComponent::CycleSelection(int32 Direction)
{
	if (Slots.IsEmpty() || Direction == 0)
	{
		return false;
	}

	const int32 NextIndex = (SelectedSlotIndex + FMath::Sign(Direction) + Slots.Num()) % Slots.Num();
	return SelectSlot(NextIndex);
}

ACSUsableToolActor* UCSQuickbarInventoryComponent::GetToolAt(int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].ToolActor.Get() : nullptr;
}

bool UCSQuickbarInventoryComponent::ContainsTool(const ACSUsableToolActor* ToolActor) const
{
	return Slots.ContainsByPredicate([ToolActor](const FCSQuickbarSlot& Slot)
	{
		return Slot.ToolActor == ToolActor;
	});
}
