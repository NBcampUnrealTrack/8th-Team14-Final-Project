#include "Features/Inventory/CSQuickbarInventoryComponent.h"

#include "Features/Tools/CSUsableToolActor.h"

UCSQuickbarInventoryComponent::UCSQuickbarInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCSQuickbarInventoryComponent::InitializeQuickbar()
{
	SetSlotCount(InventorySlotCount);
}

void UCSQuickbarInventoryComponent::SetSlotCount(int32 NewSlotCount)
{
	const int32 RequestedSlotCount = FMath::Max(1, NewSlotCount);
	if (Slots.Num() != RequestedSlotCount)
	{
		// 레벨 상승은 슬롯을 추가하기만 합니다. 기존 보관 물건은 그대로 유지합니다.
		Slots.SetNum(FMath::Max(RequestedSlotCount, Slots.Num()));
	}
	SelectedSlotIndex = FMath::Clamp(SelectedSlotIndex, 0, Slots.Num() - 1);
	OnQuickbarChanged.Broadcast();
	OnQuickbarSelectionChanged.Broadcast(SelectedSlotIndex);
}

int32 UCSQuickbarInventoryComponent::TryAddTool(ACSUsableToolActor* ToolActor)
{
	return TryAddItem(ToolActor);
}

int32 UCSQuickbarInventoryComponent::TryAddItem(AActor* ItemActor)
{
	if (!IsValid(ItemActor))
	{
		return INDEX_NONE;
	}

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		if (Slots[SlotIndex].ItemActor == ItemActor)
		{
			return SlotIndex;
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		if (!IsValid(Slots[SlotIndex].ItemActor))
		{
			Slots[SlotIndex].ItemActor = ItemActor;
			OnQuickbarChanged.Broadcast();
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

bool UCSQuickbarInventoryComponent::RemoveTool(ACSUsableToolActor* ToolActor)
{
	return RemoveItem(ToolActor);
}

bool UCSQuickbarInventoryComponent::CanAddItem(const AActor* ItemActor) const
{
	return IsValid(ItemActor) && (ContainsItem(ItemActor) ||
		Slots.ContainsByPredicate([](const FCSQuickbarSlot& Slot) { return !IsValid(Slot.ItemActor); }));
}

bool UCSQuickbarInventoryComponent::MoveItemToSlot(AActor* ItemActor, int32 TargetSlotIndex)
{
	if (!IsValid(ItemActor) || !Slots.IsValidIndex(TargetSlotIndex)) return false;
	const int32 SourceSlotIndex = Slots.IndexOfByPredicate([ItemActor](const FCSQuickbarSlot& Slot)
	{
		return Slot.ItemActor == ItemActor;
	});
	if (SourceSlotIndex == INDEX_NONE) return false;
	if (SourceSlotIndex != TargetSlotIndex)
	{
		Slots.Swap(SourceSlotIndex, TargetSlotIndex);
		OnQuickbarChanged.Broadcast();
	}
	return true;
}

bool UCSQuickbarInventoryComponent::RemoveItem(AActor* ItemActor)
{
	for (FCSQuickbarSlot& Slot : Slots)
	{
		if (Slot.ItemActor == ItemActor)
		{
			Slot.ItemActor = nullptr;
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
	return Cast<ACSUsableToolActor>(GetItemAt(SlotIndex));
}

bool UCSQuickbarInventoryComponent::ContainsTool(const ACSUsableToolActor* ToolActor) const
{
	return ContainsItem(ToolActor);
}

AActor* UCSQuickbarInventoryComponent::GetItemAt(int32 SlotIndex) const
{
	AActor* Item = Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].ItemActor.Get() : nullptr;
	return IsValid(Item) ? Item : nullptr;
}

bool UCSQuickbarInventoryComponent::ContainsItem(const AActor* ItemActor) const
{
	if (ItemActor == nullptr) return false;
	return Slots.ContainsByPredicate([ItemActor](const FCSQuickbarSlot& Slot)
	{
		return Slot.ItemActor == ItemActor;
	});
}
