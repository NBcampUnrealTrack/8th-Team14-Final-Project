#include "UI/HUD/CSQuickbarSlotWidget.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UCSQuickbarSlotWidget::SetSlotData(
	int32 SlotIndex,
	const FText& ToolName,
	UTexture2D* ToolIcon,
	bool bIsSelected)
{
	if (IsValid(SlotKeyText))
	{
		SlotKeyText->SetText(FText::AsNumber(SlotIndex + 1));
	}
	if (IsValid(ToolNameText))
	{
		ToolNameText->SetText(ToolName);
	}
	if (IsValid(ToolIconImage))
	{
		ToolIconImage->SetBrushFromTexture(ToolIcon, false);
		ToolIconImage->SetVisibility(IsValid(ToolIcon)
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}
	if (IsValid(SelectedBorder))
	{
		// 선택 테두리는 슬롯을 가리지 않고 시각적 강조만 담당합니다.
		SelectedBorder->SetVisibility(bIsSelected
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}
}
