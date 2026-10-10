#include "UI/Tutorial/CSLevelPresentationWidget.h"
#include "Systems/Tutorial/CSUnlockGuideSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"

TSharedRef<SWidget> UCSLevelPresentationWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		// WBP를 준비하시기 전에도 흐름을 확인하실 수 있도록 기본 화면을 제공합니다.
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		UScaleBox* ImageFrame = WidgetTree->ConstructWidget<UScaleBox>();
		ImageFrame->SetStretch(EStretch::ScaleToFill);
		UCanvasPanelSlot* ImageSlot = Canvas->AddChildToCanvas(ImageFrame);
		ImageSlot->SetAnchors(FAnchors(0, 0, 1, 1));
		ImageSlot->SetOffsets(FMargin(0));
		IllustrationImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IllustrationImage"));
		ImageFrame->AddChild(IllustrationImage);
		auto Panel = [&](const TCHAR* Name, FVector2D Anchor, FVector2D Position, FVector2D Alignment)
		{
			UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
			Border->SetBrushColor(FLinearColor(0.035f, 0.025f, 0.02f, 0.97f));
			Border->SetPadding(FMargin(20));
			UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Border);
			PanelSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
			PanelSlot->SetAlignment(Alignment);
			PanelSlot->SetPosition(Position);
			PanelSlot->SetAutoSize(true);
			UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
			Border->SetContent(Box);
			return Box;
		};
		auto Text = [&](UVerticalBox* Box, const TCHAR* Name, float Width)
		{
			UTextBlock* Result = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Result->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Result->SetWrapTextAt(Width);
			Box->AddChildToVerticalBox(Result)->SetPadding(FMargin(0, 5));
			return Result;
		};
		auto Button = [&](UVerticalBox* Box, const TCHAR* Name, const TCHAR* Label)
		{
			UButton* Result = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>();
			LabelText->SetText(FText::FromString(Label));
			Result->SetContent(LabelText);
			Box->AddChildToVerticalBox(Result)->SetPadding(FMargin(0, 5));
			return Result;
		};
		UVerticalBox* LevelBox = Panel(TEXT("LevelUpPanel"), FVector2D(.5f, .5f), FVector2D::ZeroVector, FVector2D(.5f, .5f));
		LevelUpPanel = LevelBox->GetParent();
		LevelUpHeadingText = Text(LevelBox, TEXT("LevelUpHeadingText"), 600);
		LevelUpBodyText = Text(LevelBox, TEXT("LevelUpBodyText"), 600);
		StoryStartButton = Button(LevelBox, TEXT("StoryStartButton"), TEXT("스토리 진행"));
		UVerticalBox* StoryBox = Panel(TEXT("StoryPanel"), FVector2D(.5f, 1), FVector2D(0, -40), FVector2D(.5f, 1));
		StoryPanel = StoryBox->GetParent();
		CaptionText = Text(StoryBox, TEXT("CaptionText"), 900);
		SpeakerText = Text(StoryBox, TEXT("SpeakerText"), 900);
		DialogueText = Text(StoryBox, TEXT("DialogueText"), 900);
		DialogueNextButton = Button(StoryBox, TEXT("DialogueNextButton"), TEXT("다음 [Space]"));
		StorySkipButton = Button(StoryBox, TEXT("StorySkipButton"), TEXT("장면 건너뛰기"));
		UVerticalBox* UnlockBox = Panel(TEXT("UnlockPanel"), FVector2D(0, .5f), FVector2D(30, 0), FVector2D(0, .5f));
		UnlockPanel = UnlockBox->GetParent();
		UnlockHeadingText = Text(UnlockBox, TEXT("UnlockHeadingText"), 400);
		UnlockBodyText = Text(UnlockBox, TEXT("UnlockBodyText"), 400);
		CameraNextButton = Button(UnlockBox, TEXT("CameraNextButton"), TEXT("다음 [Space / Enter]"));
		CameraSkipButton = Button(UnlockBox, TEXT("CameraSkipButton"), TEXT("카메라 연출 건너뛰기 [Esc]"));
		UVerticalBox* GoalBox = Panel(TEXT("GoalPanel"), FVector2D(.5f, .5f), FVector2D::ZeroVector, FVector2D(.5f, .5f));
		GoalPanel = GoalBox->GetParent();
		GoalText = Text(GoalBox, TEXT("GoalText"), 650);
		ResumeButton = Button(GoalBox, TEXT("ResumeButton"), TEXT("플레이 시작"));
	}
	SetIsFocusable(true);
	return Super::RebuildWidget();
}

void UCSLevelPresentationWidget::NativeConstruct()
{
	Super::NativeConstruct();
	for (UButton* Button : {StoryStartButton.Get(), DialogueNextButton.Get(), CameraNextButton.Get(), ResumeButton.Get()})
		if (Button) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Continue);
	for (UButton* Button : {StorySkipButton.Get(), CameraSkipButton.Get()})
		if (Button) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::SkipScene);
}

void UCSLevelPresentationWidget::NativeDestruct()
{
	for (UButton* Button : {StoryStartButton.Get(), DialogueNextButton.Get(), CameraNextButton.Get(), ResumeButton.Get()})
		if (Button) Button->OnClicked.RemoveDynamic(this, &ThisClass::Continue);
	for (UButton* Button : {StorySkipButton.Get(), CameraSkipButton.Get()})
		if (Button) Button->OnClicked.RemoveDynamic(this, &ThisClass::SkipScene);
	Super::NativeDestruct();
}

void UCSLevelPresentationWidget::Present(ECSLevelPresentationPhase Phase, const FText& Heading, const FText& Body,
	const FText& Speaker, const FText& Caption, UTexture2D* Illustration, bool bCameraWaiting)
{
	CurrentPhase = Phase;
	bAwaitingCameraContinue = bCameraWaiting;
	DisplayHeading = Heading;
	DisplayBody = Body;
	DisplaySpeaker = Speaker;
	DisplayCaption = Caption;
	DisplayIllustration = Illustration;
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto Show = [](UWidget* Target, bool bShow) { if (Target) Target->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); };
	Show(LevelUpPanel, Phase == ECSLevelPresentationPhase::LevelUp);
	Show(StoryPanel, Phase == ECSLevelPresentationPhase::Story);
	Show(UnlockPanel, Phase == ECSLevelPresentationPhase::Unlock);
	Show(GoalPanel, Phase == ECSLevelPresentationPhase::Goal);
	if (CameraNextButton) CameraNextButton->SetVisibility(Phase == ECSLevelPresentationPhase::Unlock && bCameraWaiting ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IllustrationImage)
	{
		IllustrationImage->SetBrushFromTexture(Illustration, true);
		Show(IllustrationImage, Phase == ECSLevelPresentationPhase::Story && Illustration);
	}
	if (LevelUpHeadingText) LevelUpHeadingText->SetText(Heading);
	if (LevelUpBodyText) LevelUpBodyText->SetText(Body);
	if (DialogueText) DialogueText->SetText(Body);
	if (SpeakerText) { SpeakerText->SetText(Speaker); Show(SpeakerText, !Speaker.IsEmpty()); }
	if (CaptionText) { CaptionText->SetText(Caption); Show(CaptionText, !Caption.IsEmpty()); }
	if (UnlockHeadingText) UnlockHeadingText->SetText(Heading);
	if (UnlockBodyText) UnlockBodyText->SetText(Body);
	if (GoalText) GoalText->SetText(Body);
	OnPresentationChanged();
}

void UCSLevelPresentationWidget::Continue()
{
	if (UCSUnlockGuideSubsystem* Guide = GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()) Guide->ContinuePresentation();
}

void UCSLevelPresentationWidget::SkipScene()
{
	if (UCSUnlockGuideSubsystem* Guide = GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>()) Guide->SkipStoryScene();
}

FReply UCSLevelPresentationWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	// 길게 누르셔도 여러 대사가 한꺼번에 넘어가지 않도록 반복 입력을 무시합니다.
	if (Event.GetKey() == EKeys::SpaceBar || Event.GetKey() == EKeys::Enter)
	{
		if (!Event.IsRepeat() && (CurrentPhase == ECSLevelPresentationPhase::Story ||
			(CurrentPhase == ECSLevelPresentationPhase::Unlock && bAwaitingCameraContinue))) Continue();
		return FReply::Handled();
	}
	if (Event.GetKey() == EKeys::Escape)
	{
		if (!Event.IsRepeat() && CurrentPhase == ECSLevelPresentationPhase::Unlock) SkipScene();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
