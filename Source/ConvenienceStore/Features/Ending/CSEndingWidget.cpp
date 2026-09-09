#include "Features/Ending/CSEndingWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

TSharedRef<SWidget> UCSEndingWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor::Black);
		Background->SetHorizontalAlignment(HAlign_Center);
		Background->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Message = WidgetTree->ConstructWidget<UTextBlock>();
		Message->SetText(NSLOCTEXT("Ending", "Placeholder", "귀환의 문이 열렸습니다.\n\n엔딩 연출 준비 중"));
		Message->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Message->SetJustification(ETextJustify::Center);
		Background->SetContent(Message);
		WidgetTree->RootWidget = Background;
	}
	return Super::RebuildWidget();
}
