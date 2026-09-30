#include "UI/HUD/CSInteractionPanelWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UCSInteractionPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || !InteractionPromptRow || !InteractionPromptText) return;
	UPanelWidget* Parent = InteractionPromptRow->GetParent();
	UPanelSlot* PromptSlot = InteractionPromptRow->Slot;
	if (!Parent || !PromptSlot || PromptSlot->Parent != Parent) return;

	// Slate가 생성되기 전에 기존 슬롯을 유지하며 안내 영역을 세로 목록으로 확장합니다.
	UVerticalBox* PromptList = WidgetTree->ConstructWidget<UVerticalBox>();
	PromptSlot->Content = PromptList;
	PromptList->Slot = PromptSlot;
	PromptSlot->SynchronizeProperties();
	InteractionPromptRow->Slot = nullptr;
	PromptList->AddChildToVerticalBox(InteractionPromptRow);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UOverlay* Row = WidgetTree->ConstructWidget<UOverlay>();
		UImage* Background = WidgetTree->ConstructWidget<UImage>();
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		if (InteractionPromptBackground) Background->SetBrush(InteractionPromptBackground->GetBrush());
		Text->SetFont(InteractionPromptText->GetFont());
		Text->SetShadowOffset(InteractionPromptText->GetShadowOffset());
		Text->SetShadowColorAndOpacity(InteractionPromptText->GetShadowColorAndOpacity());
		UOverlaySlot* BackgroundSlot = Row->AddChildToOverlay(Background);
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
		UOverlaySlot* TextSlot = Row->AddChildToOverlay(Text);
		TextSlot->SetPadding(FMargin(8.0f, 3.0f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		if (const UOverlaySlot* TemplateSlot = Cast<UOverlaySlot>(InteractionPromptText->Slot))
		{
			TextSlot->SetPadding(TemplateSlot->GetPadding());
			TextSlot->SetHorizontalAlignment(TemplateSlot->GetHorizontalAlignment());
			TextSlot->SetVerticalAlignment(TemplateSlot->GetVerticalAlignment());
		}
		PromptList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
		AdditionalPromptRows.Add(Row);
		AdditionalPromptBackgrounds.Add(Background);
		AdditionalPromptTexts.Add(Text);
		Row->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCSInteractionPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ClearInteractionContext();
}

void UCSInteractionPanelWidget::SetInteractionPrompt(
	const FText& Message,
	const FLinearColor& RequestedColor)
{
	for (UWidget* Row : AdditionalPromptRows) Row->SetVisibility(ESlateVisibility::Collapsed);
	SetRow(
		InteractionPromptRow,
		InteractionPromptBackground,
		InteractionPromptText,
		Message,
		ResolveRequestedStyle(RequestedColor));
}

void UCSInteractionPanelWidget::SetInteractionPrompts(const TArray<FCSInteractionPrompt>& Prompts)
{
	// 문구의 내용 대신 게임플레이에서 전달한 판정으로 각 행의 위험 스타일을 선택합니다.
	SetInteractionPrompt(Prompts.IsEmpty() ? FText::GetEmpty() : Prompts[0].Message,
		!Prompts.IsEmpty() && Prompts[0].bDanger ? FLinearColor::Red : FLinearColor::White);
	for (int32 Index = 0; Index < AdditionalPromptRows.Num(); ++Index)
	{
		const int32 PromptIndex = Index + 1;
		const bool bHasPrompt = Prompts.IsValidIndex(PromptIndex);
		SetRow(AdditionalPromptRows[Index], AdditionalPromptBackgrounds[Index], AdditionalPromptTexts[Index],
			bHasPrompt ? Prompts[PromptIndex].Message : FText::GetEmpty(),
			bHasPrompt && Prompts[PromptIndex].bDanger ? EInteractionVisualStyle::Danger : EInteractionVisualStyle::Normal);
	}
}

void UCSInteractionPanelWidget::SetProductPrice(const FText& Message)
{
	SetRow(
		ProductPriceRow,
		ProductPriceBackground,
		ProductPriceText,
		Message,
		EInteractionVisualStyle::Price);
}

void UCSInteractionPanelWidget::SetProductShelfLife(
	const FText& Message,
	const FLinearColor& RequestedColor)
{
	SetRow(
		ProductShelfLifeRow,
		ProductShelfLifeBackground,
		ProductShelfLifeText,
		Message,
		ResolveRequestedStyle(RequestedColor));
}

void UCSInteractionPanelWidget::SetProductFreshness(
	const FText& Message,
	const FLinearColor& RequestedColor)
{
	SetRow(
		ProductFreshnessRow,
		ProductFreshnessBackground,
		ProductFreshnessText,
		Message,
		ResolveRequestedStyle(RequestedColor));
}

void UCSInteractionPanelWidget::SetStorageWarning(const FText& Message)
{
	SetRow(
		StorageWarningRow,
		StorageWarningBackground,
		StorageWarningText,
		Message,
		EInteractionVisualStyle::Warning);
}

void UCSInteractionPanelWidget::ClearInteractionContext()
{
	for (UWidget* Row : AdditionalPromptRows) Row->SetVisibility(ESlateVisibility::Collapsed);
	UWidget* const Rows[] = {
		InteractionPromptRow.Get(),
		ProductPriceRow.Get(),
		ProductShelfLifeRow.Get(),
		ProductFreshnessRow.Get(),
		StorageWarningRow.Get()
	};

	for (UWidget* Row : Rows)
	{
		if (IsValid(Row))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

UCSInteractionPanelWidget::EInteractionVisualStyle
UCSInteractionPanelWidget::ResolveRequestedStyle(const FLinearColor& RequestedColor) const
{
	// 기존 호출부의 RGB 의미만 분류하고 실제 표시색은 Class Defaults 값을 사용합니다.
	if (RequestedColor.G < 0.5f)
	{
		return EInteractionVisualStyle::Danger;
	}

	if (RequestedColor.G < 0.9f)
	{
		return EInteractionVisualStyle::Warning;
	}

	return EInteractionVisualStyle::Normal;
}

void UCSInteractionPanelWidget::SetRow(
	UWidget* Row,
	UImage* Background,
	UTextBlock* TextBlock,
	const FText& Message,
	EInteractionVisualStyle Style)
{
	if (!IsValid(Row) || !IsValid(TextBlock))
	{
		return;
	}

	if (Message.IsEmpty())
	{
		Row->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	TextBlock->SetText(Message);
	TextBlock->SetColorAndOpacity(FSlateColor(GetTextColor(Style)));

	if (IsValid(Background))
	{
		UTexture2D* BackgroundTexture = Style == EInteractionVisualStyle::Danger
			? DangerBackgroundTexture
			: NormalBackgroundTexture;
		if (IsValid(BackgroundTexture))
		{
			Background->SetBrushFromTexture(BackgroundTexture, false);
		}
	}

	Row->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FLinearColor UCSInteractionPanelWidget::GetTextColor(
	EInteractionVisualStyle Style) const
{
	switch (Style)
	{
	case EInteractionVisualStyle::Warning:
		return WarningTextColor;
	case EInteractionVisualStyle::Danger:
		return DangerTextColor;
	case EInteractionVisualStyle::Price:
		return PriceTextColor;
	case EInteractionVisualStyle::Normal:
	default:
		return NormalTextColor;
	}
}
