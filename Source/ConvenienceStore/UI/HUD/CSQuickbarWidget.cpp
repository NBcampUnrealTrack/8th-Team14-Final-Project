#include "UI/HUD/CSQuickbarWidget.h"

#include "Components/HorizontalBox.h"
#include "UI/HUD/CSQuickbarSlotWidget.h"

void UCSQuickbarWidget::SetQuickbarData(
	int32 SlotCount,
	int32 SelectedSlotIndex,
	const TArray<FText>& ToolNames,
	const TArray<UTexture2D*>& ToolIcons)
{
	if (!IsValid(SlotContainer) || QuickbarSlotWidgetClass == nullptr || SlotCount < 1)
	{
		return;
	}

	if (CreatedSlotWidgets.Num() != SlotCount)
	{
		SlotContainer->ClearChildren();
		CreatedSlotWidgets.Reset();
		CreatedSlotWidgets.Reserve(SlotCount);
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			UCSQuickbarSlotWidget* SlotWidget = CreateWidget<UCSQuickbarSlotWidget>(
				this,
				QuickbarSlotWidgetClass);
			if (!IsValid(SlotWidget))
			{
				continue;
			}
			SlotContainer->AddChild(SlotWidget);
			CreatedSlotWidgets.Add(SlotWidget);
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < CreatedSlotWidgets.Num(); ++SlotIndex)
	{
		UCSQuickbarSlotWidget* SlotWidget = CreatedSlotWidgets[SlotIndex];
		if (!IsValid(SlotWidget))
		{
			continue;
		}
		const FText ToolName = ToolNames.IsValidIndex(SlotIndex)
			? ToolNames[SlotIndex]
			: FText::GetEmpty();
		UTexture2D* ToolIcon = ToolIcons.IsValidIndex(SlotIndex)
			? ToolIcons[SlotIndex]
			: nullptr;
		SlotWidget->SetSlotData(
			SlotIndex,
			ToolName,
			ToolIcon,
			SlotIndex == SelectedSlotIndex);
	}
}
