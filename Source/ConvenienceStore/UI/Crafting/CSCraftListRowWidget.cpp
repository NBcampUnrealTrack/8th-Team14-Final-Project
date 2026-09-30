#include "UI/Crafting/CSCraftListRowWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UCSCraftListRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SelectButton->OnClicked.AddUniqueDynamic(this, &UCSCraftListRowWidget::HandleSelectClicked);
}

void UCSCraftListRowWidget::NativeDestruct()
{
	SelectButton->OnClicked.RemoveDynamic(this, &UCSCraftListRowWidget::HandleSelectClicked);
	OnSelected.Unbind();
	Super::NativeDestruct();
}

void UCSCraftListRowWidget::SetEntry(
	FName InKey,
	const FText& Name,
	const FText& Detail,
	int32 Count,
	UTexture2D* Icon,
	bool bSelectable,
	FCSCraftListRowSelected InOnSelected)
{
	Key = InKey;
	OnSelected = MoveTemp(InOnSelected);
	NameText->SetText(Name);
	DetailText->SetText(Detail);
	DetailText->SetVisibility(Detail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	CountText->SetText(FText::AsNumber(Count));
	SelectButton->SetIsEnabled(bSelectable);
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

void UCSCraftListRowWidget::HandleSelectClicked()
{
	OnSelected.ExecuteIfBound(Key);
}
