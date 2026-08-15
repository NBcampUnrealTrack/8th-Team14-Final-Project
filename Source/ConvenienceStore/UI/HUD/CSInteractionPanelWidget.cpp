#include "UI/HUD/CSInteractionPanelWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"

void UCSInteractionPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ClearInteractionContext();
}

void UCSInteractionPanelWidget::SetInteractionPrompt(
	const FText& Message,
	const FLinearColor& RequestedColor)
{
	SetRow(
		InteractionPromptRow,
		InteractionPromptBackground,
		InteractionPromptText,
		Message,
		ResolveRequestedStyle(RequestedColor));
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
