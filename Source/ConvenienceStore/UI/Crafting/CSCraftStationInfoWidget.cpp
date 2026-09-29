#include "UI/Crafting/CSCraftStationInfoWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UCSCraftStationInfoWidget::SetIngredient(int32 SlotIndex, UTexture2D* Icon, int32 Count)
{
	UImage* IconImage = SlotIndex == 0 ? IngredientIconA : IngredientIconB;
	UTextBlock* CountText = SlotIndex == 0 ? IngredientCountA : IngredientCountB;

	// 아이콘이 없는 상품이 많다. 빈 이미지는 흰 사각형으로 보여서 숨긴다
	if (Icon != nullptr && Count > 0)
	{
		IconImage->SetBrushFromTexture(Icon);
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	CountText->SetText(Count > 0 ? FText::AsNumber(Count) : FText::GetEmpty());
}

void UCSCraftStationInfoWidget::SetResultName(const FText& ResultName)
{
	ResultNameText->SetText(ResultName);
}

void UCSCraftStationInfoWidget::SetStatus(const FText& Status)
{
	StatusText->SetText(Status);
}
