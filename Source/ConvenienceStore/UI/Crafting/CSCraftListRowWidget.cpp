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
	// 도감처럼 개수가 없는 목록은 음수를 넘겨 칸을 접는다. 0은 "재고 0"이라 그대로 보여 준다
	CountText->SetText(FText::AsNumber(Count));
	CountText->SetVisibility(Count < 0 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
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
