#include "Features/Ending/CSEndingWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/KismetSystemLibrary.h"

TSharedRef<SWidget> UCSEndingWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor::Black);
		Background->SetHorizontalAlignment(HAlign_Center);
		Background->SetVerticalAlignment(VAlign_Center);
		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		UTextBlock* Message = WidgetTree->ConstructWidget<UTextBlock>();
		Message->SetText(NSLOCTEXT("Ending", "Placeholder", "귀환의 문이 열렸습니다.\n\n엔딩 연출 준비 중"));
		Message->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Message->SetJustification(ETextJustify::Center);
		Content->AddChildToVerticalBox(Message)->SetHorizontalAlignment(HAlign_Center);
		// 월드가 일시정지된 상태에서도 UI 클릭으로 플레이를 종료할 수 있습니다.
		QuitButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("QuitButton"));
		UTextBlock* ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonLabel->SetText(NSLOCTEXT("Ending", "QuitGame", "게임 종료"));
		ButtonLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		ButtonLabel->SetMargin(FMargin(32.0f, 12.0f));
		QuitButton->SetContent(ButtonLabel);
		UVerticalBoxSlot* ButtonSlot = Content->AddChildToVerticalBox(QuitButton);
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonSlot->SetPadding(FMargin(0.0f, 32.0f, 0.0f, 0.0f));
		Background->SetContent(Content);
		WidgetTree->RootWidget = Background;
	}
	return Super::RebuildWidget();
}

void UCSEndingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(QuitButton))
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UCSEndingWidget::QuitEndingGame);
	}
}

void UCSEndingWidget::NativeDestruct()
{
	if (IsValid(QuitButton))
	{
		QuitButton->OnClicked.RemoveDynamic(this, &UCSEndingWidget::QuitEndingGame);
	}
	Super::NativeDestruct();
}

void UCSEndingWidget::QuitEndingGame()
{
	// 엔진의 종료 경로를 사용하여 패키지에서는 앱을, PIE에서는 플레이 세션을 종료합니다.
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
