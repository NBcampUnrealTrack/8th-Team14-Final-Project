#include "UI/Tutorial/CSUnlockGuideWidget.h"
#include "Systems/Tutorial/CSUnlockGuideSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"

UCSUnlockGuideWidget::UCSUnlockGuideWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SummaryLayout.Anchor = FVector2D(0.5f, 0.5f);
	SummaryLayout.Alignment = FVector2D(0.5f, 0.5f);
	SummaryLayout.Position = FVector2D::ZeroVector;
	SummaryLayout.Size = FVector2D(560.0f, 460.0f);
	SummaryLayout.BodyHeight = 320.0f;
}

TSharedRef<SWidget> UCSUnlockGuideWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		// 별도 WBP를 준비하지 않으셔도 사용할 수 있도록 기본 레이아웃을 구성합니다.
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		GuidePanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("GuidePanel"));
		GuidePanel->SetBrushColor(FLinearColor(0.035f, 0.025f, 0.02f, 0.97f));
		GuidePanel->SetPadding(FMargin(20.0f));
		Canvas->AddChildToCanvas(GuidePanel)->SetAutoSize(true);
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		GuidePanel->SetContent(Box);
		HeadingText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeadingText"));
		HeadingText->SetFont(FSlateFontInfo(HeadingText->GetFont().FontObject, 22));
		HeadingText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		HeadingText->SetWrapTextAt(520.0f);
		Box->AddChildToVerticalBox(HeadingText);
		BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BodyText"));
		BodyText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		BodyText->SetWrapTextAt(520.0f);
		// 설명이 여러 개일 때도 화면 밖으로 넘치지 않도록 스크롤 영역을 제공합니다.
		BodyArea = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BodyArea"));
		BodyArea->SetWidthOverride(520.0f);
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Scroll->AddChild(BodyText);
		BodyArea->SetContent(Scroll);
		Box->AddChildToVerticalBox(BodyArea)->SetPadding(FMargin(0.0f, 12.0f));
		auto AddButton = [&](const TCHAR* Name, const TCHAR* Label)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
			Text->SetText(FText::FromString(Label));
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
			Button->SetContent(Text);
			Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.0f, 3.0f));
			return Button;
		};
		ViewButton = AddButton(TEXT("ViewButton"), TEXT("안내 보기 / 펼치기"));
		LaterButton = AddButton(TEXT("LaterButton"), TEXT("나중에 보기"));
		DismissButton = AddButton(TEXT("DismissButton"), TEXT("안내 생략"));
		CancelButton = AddButton(TEXT("CancelButton"), TEXT("안내 요청 취소"));
		SkipButton = AddButton(TEXT("SkipButton"), TEXT("카메라 건너뛰기 [Esc]"));
		CloseButton = AddButton(TEXT("CloseButton"), TEXT("닫기"));
	}
	SetIsFocusable(true);
	return Super::RebuildWidget();
}

void UCSUnlockGuideWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ViewButton) ViewButton->OnClicked.AddUniqueDynamic(this, &ThisClass::View);
	if (LaterButton) LaterButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Later);
	if (DismissButton) DismissButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Dismiss);
	if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Cancel);
	if (SkipButton) SkipButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Skip);
	if (CloseButton) CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Close);
}

void UCSUnlockGuideWidget::NativeDestruct()
{
	if (ViewButton) ViewButton->OnClicked.RemoveDynamic(this, &ThisClass::View);
	if (LaterButton) LaterButton->OnClicked.RemoveDynamic(this, &ThisClass::Later);
	if (DismissButton) DismissButton->OnClicked.RemoveDynamic(this, &ThisClass::Dismiss);
	if (CancelButton) CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::Cancel);
	if (SkipButton) SkipButton->OnClicked.RemoveDynamic(this, &ThisClass::Skip);
	if (CloseButton) CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::Close);
	Super::NativeDestruct();
}

void UCSUnlockGuideWidget::Present(const FText& Heading, const FText& Body, bool bCollapsed, bool bWaiting, bool bCamera, bool bSummary)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	const bool bSeparatePanels = NotificationPanel && SummaryPanel;
	const FCSUnlockGuideLayout& Layout = bSummary ? SummaryLayout : NotificationLayout;
	if (bSeparatePanels)
	{
		NotificationPanel->SetVisibility(bSummary ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		SummaryPanel->SetVisibility(bSummary ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (SummaryHeadingText) SummaryHeadingText->SetText(Heading);
		if (SummaryBodyText) SummaryBodyText->SetText(Body);
		if (SummaryBodyArea && bApplyLayoutSettings) SummaryBodyArea->SetHeightOverride(SummaryLayout.BodyHeight);
	}
	if (HeadingText) HeadingText->SetText(Heading);
	if (BodyText) { BodyText->SetText(Body); BodyText->SetVisibility(bCollapsed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible); }
	if (BodyArea)
	{
		if (bApplyLayoutSettings) BodyArea->SetHeightOverride(bSeparatePanels ? NotificationLayout.BodyHeight : Layout.BodyHeight);
		BodyArea->SetVisibility(bCollapsed || bCamera ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	auto Show = [](UWidget* Widget, bool bVisible) { if (Widget) Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); };
	Show(ViewButton, !bWaiting && !bCamera && !bSummary);
	Show(LaterButton, !bCollapsed && !bWaiting && !bCamera && !bSummary);
	Show(DismissButton, !bCollapsed && !bWaiting && !bCamera && !bSummary);
	Show(CancelButton, bWaiting);
	Show(SkipButton, bCamera);
	Show(CloseButton, bSummary);
	// 분리된 패널은 각각 Canvas 바로 아래에 배치해 주세요. 공통 부모의 배치는 변경하지 않습니다.
	UWidget* LayoutPanel = bSeparatePanels ? (bSummary ? SummaryPanel.Get() : NotificationPanel.Get()) : GuidePanel.Get();
	if (LayoutPanel && bApplyLayoutSettings)
	{
		if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(LayoutPanel->Slot))
		{
			PanelSlot->SetAnchors(FAnchors(Layout.Anchor.X, Layout.Anchor.Y));
			PanelSlot->SetAlignment(Layout.Alignment);
			PanelSlot->SetPosition(Layout.Position);
			PanelSlot->SetAutoSize(Layout.bAutoSize);
			if (!Layout.bAutoSize) PanelSlot->SetSize(Layout.Size);
		}
	}
	if (bUpdateButtonLabels)
	{
		auto Label = [](UButton* Button, const FString& Text)
		{
			if (Button) if (UTextBlock* TextBlock = Cast<UTextBlock>(Button->GetContent())) TextBlock->SetText(FText::FromString(Text));
		};
		Label(ViewButton, bCollapsed ? TEXT("알림 펼치기") : TEXT("안내 보기"));
		Label(LaterButton, TEXT("나중에 보기"));
		Label(DismissButton, TEXT("안내 생략"));
		Label(CancelButton, TEXT("안내 요청 취소"));
	}
	OnGuidePresented(Heading, Body, bCollapsed, bWaiting, bCamera, bSummary);
}

void UCSUnlockGuideWidget::View() { GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()->ViewGuide(); }
void UCSUnlockGuideWidget::Later() { GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()->DeferGuide(); }
void UCSUnlockGuideWidget::Dismiss() { GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()->DismissGuide(); }
void UCSUnlockGuideWidget::Cancel() { GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()->CancelRequest(); }
void UCSUnlockGuideWidget::Skip() { GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()->SkipCamera(); }
void UCSUnlockGuideWidget::Close() { GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()->CloseSummary(); }

FReply UCSUnlockGuideWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape) { Skip(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry, Event);
}
