#include "Features/NoticeBoard/UI/CSNoticeBoardWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Features/NoticeBoard/CSNoticeBoardActor.h"

void UCSNoticeBoardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(Button_Weather))
	{
		Button_Weather->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleWeatherClicked);
		Button_Weather->OnHovered.AddUniqueDynamic(this, &ThisClass::HandleWeatherHovered);
		Button_Weather->OnUnhovered.AddUniqueDynamic(this, &ThisClass::HandleWeatherUnhovered);
	}
	if (IsValid(Button_Trend))
	{
		Button_Trend->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleTrendClicked);
		Button_Trend->OnHovered.AddUniqueDynamic(this, &ThisClass::HandleTrendHovered);
		Button_Trend->OnUnhovered.AddUniqueDynamic(this, &ThisClass::HandleTrendUnhovered);
	}
	if (IsValid(Button_Event))
	{
		Button_Event->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleEventClicked);
		Button_Event->OnHovered.AddUniqueDynamic(this, &ThisClass::HandleEventHovered);
		Button_Event->OnUnhovered.AddUniqueDynamic(this, &ThisClass::HandleEventUnhovered);
	}
	if (IsValid(Button_Delivery))
	{
		Button_Delivery->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDeliveryClicked);
		Button_Delivery->OnHovered.AddUniqueDynamic(this, &ThisClass::HandleDeliveryHovered);
		Button_Delivery->OnUnhovered.AddUniqueDynamic(this, &ThisClass::HandleDeliveryUnhovered);
	}

	RefreshTabPresentations();
}

void UCSNoticeBoardWidget::InitializeNoticeBoard(ACSNoticeBoardActor* InNoticeBoardActor)
{
	NoticeBoardActor = InNoticeBoardActor;
	if (!IsValid(NoticeBoardActor))
	{
		return;
	}

	InitializeNoticeBoardView();
	ShowNoticeBoardTab(SelectedTabId);
}

void UCSNoticeBoardWidget::CloseNoticeBoardWindow()
{
	if (IsValid(NoticeBoardActor))
	{
		NoticeBoardActor->CloseNoticeBoardUI(this);
	}
}

void UCSNoticeBoardWidget::SelectTab(const FName TabId)
{
	if (TabId.IsNone())
	{
		return;
	}

	SelectedTabId = TabId;
	RefreshTabPresentations();
	ShowNoticeBoardTab(SelectedTabId);
}

void UCSNoticeBoardWidget::RefreshTabPresentations()
{
	RefreshTabPresentation(TEXT("Weather"), SelBg_Weather, Txt_WeatherTitle, Txt_WeatherDescription);
	RefreshTabPresentation(TEXT("Trend"), SelBg_Trend, Txt_TrendTitle, Txt_TrendDescription);
	RefreshTabPresentation(TEXT("Event"), SelBg_Event, Txt_EventTitle, Txt_EventDescription);
	RefreshTabPresentation(TEXT("Delivery"), SelBg_Delivery, Txt_DeliveryTitle, Txt_DeliveryDescription);
}

void UCSNoticeBoardWidget::RefreshTabPresentation(
	const FName TabId,
	UWidget* SelectedBackground,
	UTextBlock* TitleText,
	UTextBlock* DescriptionText) const
{
	const bool bSelected = SelectedTabId == TabId;
	const bool bHighlighted = bSelected || HoveredTabId == TabId;

	if (IsValid(SelectedBackground))
	{
		SelectedBackground->SetVisibility(
			bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(TitleText))
	{
		TitleText->SetColorAndOpacity(
			bHighlighted ? HighlightedTabTitleColor : DefaultTabTitleColor);
	}
	if (IsValid(DescriptionText))
	{
		DescriptionText->SetColorAndOpacity(
			bHighlighted ? HighlightedTabDescriptionColor : DefaultTabDescriptionColor);
	}
}

void UCSNoticeBoardWidget::SetHoveredTab(const FName TabId, const bool bHovered)
{
	if (bHovered)
	{
		HoveredTabId = TabId;
	}
	else if (HoveredTabId == TabId)
	{
		HoveredTabId = NAME_None;
	}
	RefreshTabPresentations();
}

void UCSNoticeBoardWidget::HandleWeatherHovered() { SetHoveredTab(TEXT("Weather"), true); }
void UCSNoticeBoardWidget::HandleWeatherUnhovered() { SetHoveredTab(TEXT("Weather"), false); }
void UCSNoticeBoardWidget::HandleWeatherClicked() { SelectTab(TEXT("Weather")); }
void UCSNoticeBoardWidget::HandleTrendHovered() { SetHoveredTab(TEXT("Trend"), true); }
void UCSNoticeBoardWidget::HandleTrendUnhovered() { SetHoveredTab(TEXT("Trend"), false); }
void UCSNoticeBoardWidget::HandleTrendClicked() { SelectTab(TEXT("Trend")); }
void UCSNoticeBoardWidget::HandleEventHovered() { SetHoveredTab(TEXT("Event"), true); }
void UCSNoticeBoardWidget::HandleEventUnhovered() { SetHoveredTab(TEXT("Event"), false); }
void UCSNoticeBoardWidget::HandleEventClicked() { SelectTab(TEXT("Event")); }
void UCSNoticeBoardWidget::HandleDeliveryHovered() { SetHoveredTab(TEXT("Delivery"), true); }
void UCSNoticeBoardWidget::HandleDeliveryUnhovered() { SetHoveredTab(TEXT("Delivery"), false); }
void UCSNoticeBoardWidget::HandleDeliveryClicked() { SelectTab(TEXT("Delivery")); }

void UCSNoticeBoardWidget::NativeDestruct()
{
	if (IsValid(Button_Weather))
	{
		Button_Weather->OnClicked.RemoveDynamic(this, &ThisClass::HandleWeatherClicked);
		Button_Weather->OnHovered.RemoveDynamic(this, &ThisClass::HandleWeatherHovered);
		Button_Weather->OnUnhovered.RemoveDynamic(this, &ThisClass::HandleWeatherUnhovered);
	}
	if (IsValid(Button_Trend))
	{
		Button_Trend->OnClicked.RemoveDynamic(this, &ThisClass::HandleTrendClicked);
		Button_Trend->OnHovered.RemoveDynamic(this, &ThisClass::HandleTrendHovered);
		Button_Trend->OnUnhovered.RemoveDynamic(this, &ThisClass::HandleTrendUnhovered);
	}
	if (IsValid(Button_Event))
	{
		Button_Event->OnClicked.RemoveDynamic(this, &ThisClass::HandleEventClicked);
		Button_Event->OnHovered.RemoveDynamic(this, &ThisClass::HandleEventHovered);
		Button_Event->OnUnhovered.RemoveDynamic(this, &ThisClass::HandleEventUnhovered);
	}
	if (IsValid(Button_Delivery))
	{
		Button_Delivery->OnClicked.RemoveDynamic(this, &ThisClass::HandleDeliveryClicked);
		Button_Delivery->OnHovered.RemoveDynamic(this, &ThisClass::HandleDeliveryHovered);
		Button_Delivery->OnUnhovered.RemoveDynamic(this, &ThisClass::HandleDeliveryUnhovered);
	}
	HoveredTabId = NAME_None;

	// 외부에서 RemoveFromParent를 호출한 경우에도 액터가 오래된 위젯을 보관하지 않게 합니다.
	if (IsValid(NoticeBoardActor))
	{
		NoticeBoardActor->CloseNoticeBoardUI(this);
	}
	Super::NativeDestruct();
}
