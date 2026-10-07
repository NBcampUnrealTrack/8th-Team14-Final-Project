#include "UI/Crafting/CSCraftIngredientRowWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UCSCraftIngredientRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RemoveButton->OnClicked.AddUniqueDynamic(this, &UCSCraftIngredientRowWidget::HandleRemoveClicked);
}

void UCSCraftIngredientRowWidget::NativeDestruct()
{
	RemoveButton->OnClicked.RemoveDynamic(this, &UCSCraftIngredientRowWidget::HandleRemoveClicked);
	OnRemove.Unbind();
	Super::NativeDestruct();
}

void UCSCraftIngredientRowWidget::SetEntry(
	FName InProductId,
	const FText& Name,
	int32 Count,
	UTexture2D* Icon,
	const FText& RemoveLabel,
	FCSCraftIngredientRemoveClicked InOnRemove)
{
	ProductId = InProductId;
	OnRemove = MoveTemp(InOnRemove);
	NameText->SetText(Name);
	CountText->SetText(FText::AsNumber(Count));
	RemoveButtonText->SetText(RemoveLabel);
	if (IsValid(Icon))
	{
		IconImage->SetBrushFromTexture(Icon);
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCSCraftIngredientRowWidget::HandleRemoveClicked()
{
	OnRemove.ExecuteIfBound(ProductId);
}
