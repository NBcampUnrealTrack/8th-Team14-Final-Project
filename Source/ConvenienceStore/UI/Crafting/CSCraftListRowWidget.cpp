#include "UI/Crafting/CSCraftListRowWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
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
	SelectButton->SetVisibility(ESlateVisibility::Visible);
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

void UCSCraftListRowWidget::SetAttemptPresentation(const FText& ResultLine, const FText& FailureDetail, bool bSucceeded)
{
	// Disabled는 기록까지 회색으로 바꾸므로 입력만 막고 읽기 대비는 유지한다.
	SelectButton->SetIsEnabled(true);
	SetIsFocusable(false);
	SelectButton->SetVisibility(ESlateVisibility::HitTestInvisible);
	OnSelected.Unbind();
	FSlateFontInfo AttemptFont = NameText->GetFont();
	AttemptFont.TypefaceFontName = AttemptTypeface;
	NameText->SetFont(AttemptFont);
	NameText->SetColorAndOpacity(AttemptNameColor);
	DetailText->SetColorAndOpacity(AttemptDetailColor);

	if (ResultLine.IsEmpty())
	{
		return;
	}

	if (UVerticalBox* TextColumn = Cast<UVerticalBox>(DetailText->GetParent()))
	{
		CountText->RemoveFromParent();
		UVerticalBoxSlot* ResultSlot = TextColumn->AddChildToVerticalBox(CountText);
		ResultSlot->SetHorizontalAlignment(HAlign_Fill);
		ResultSlot->SetPadding(FMargin(0.0f, AttemptResultGap, 0.0f, 0.0f));
	}
	CountText->SetText(FailureDetail.IsEmpty() ? ResultLine : FText::Join(
		FText::FromString(TEXT("\n")), TArray<FText>{ ResultLine, FailureDetail }));
	CountText->SetAutoWrapText(true);
	CountText->SetJustification(ETextJustify::Left);
	CountText->SetVisibility(ESlateVisibility::HitTestInvisible);
	CountText->SetColorAndOpacity(bSucceeded ? AttemptSuccessColor : AttemptFailureColor);

	bAttemptCard = true;
	bAttemptSucceeded = bSucceeded;
	ApplyButtonStyle();
}

void UCSCraftListRowWidget::SetAutoRecipePresentation()
{
	// 같은 행을 도감도 쓰므로 재료 두 줄의 여백은 자동 제조에서만 늘린다.
	bAutoRecipeLayout = true;
	ApplyButtonStyle();
	if (UVerticalBoxSlot* DetailSlot = Cast<UVerticalBoxSlot>(DetailText->Slot))
	{
		DetailSlot->SetPadding(FMargin(0.0f, AutoRecipeDetailGap, 0.0f, 0.0f));
	}
}

void UCSCraftListRowWidget::SetSelected(bool bInSelected)
{
	if (bSelected != bInSelected)
	{
		bSelected = bInSelected;
		ApplyButtonStyle();
	}
}

void UCSCraftListRowWidget::ApplyButtonStyle()
{
	if (!bBaseStyleCached)
	{
		BaseStyle = SelectButton->GetStyle();
		bBaseStyleCached = true;
	}

	FButtonStyle Style = BaseStyle;
	if (bSelected)
	{
		Style.Normal = BaseStyle.Hovered;
		Style.NormalForeground = BaseStyle.HoveredForeground;
	}
	if (bAutoRecipeLayout)
	{
		Style.NormalPadding = AutoRecipeNormalPadding;
		Style.PressedPadding = AutoRecipePressedPadding;
	}
	if (bAttemptCard)
	{
		Style.Normal.TintColor = bAttemptSucceeded ? AttemptSuccessCardTint : AttemptFailureCardTint;
		Style.Normal.OutlineSettings.Color = bAttemptSucceeded ? AttemptSuccessOutline : AttemptFailureOutline;
		Style.Normal.OutlineSettings.Width = AttemptOutlineWidth;
	}
	SelectButton->SetStyle(Style);
}

void UCSCraftListRowWidget::HandleSelectClicked()
{
	OnSelected.ExecuteIfBound(Key);
}
