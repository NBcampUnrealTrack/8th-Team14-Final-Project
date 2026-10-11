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
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformTime.h"

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
	LastCinematicRealTime = FPlatformTime::Seconds();
	bCinematicModeActive = false;
	LetterboxProgress = 0.0f;
	PortraitOpacity = 0.0f;
	LevelUpPopElapsed = 0.0f;
	bLevelUpPopPlaying = false;
	bHasPresented = false;
	UnlockShotElapsed = 0.0f;
	UnlockShotDuration = 0.0f;
	bUnlockShotPlaying = false;
	ActiveSpeakerPortrait = nullptr;
	UpdateCinematicVisuals();
	for (UButton* Button : {StoryStartButton.Get(), DialogueNextButton.Get(), CameraNextButton.Get(), ResumeButton.Get()})
		if (Button) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Continue);
	for (UButton* Button : {StorySkipButton.Get(), CameraSkipButton.Get()})
		if (Button) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::SkipScene);
}

void UCSLevelPresentationWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const double Now = FPlatformTime::Seconds();
	const float RealDelta = FMath::Clamp(static_cast<float>(Now - LastCinematicRealTime), 0.0f, 0.1f);
	LastCinematicRealTime = Now;
	const float LetterboxStep = RealDelta / FMath::Max(0.05f, LetterboxAnimationDuration);
	const float PortraitStep = RealDelta / FMath::Max(0.05f, PortraitFadeDuration);
	LetterboxProgress = FMath::Clamp(LetterboxProgress + (bCinematicModeActive ? LetterboxStep : -LetterboxStep), 0.0f, 1.0f);
	const bool bPortraitShouldShow = bCinematicModeActive && IsValid(ActiveSpeakerPortrait);
	PortraitOpacity = FMath::Clamp(PortraitOpacity + (bPortraitShouldShow ? PortraitStep : -PortraitStep), 0.0f, 1.0f);
	if (bLevelUpPopPlaying && LevelUpPanel)
	{
		LevelUpPopElapsed += RealDelta;
		const float Progress = FMath::Clamp(LevelUpPopElapsed / FMath::Max(0.1f, LevelUpPopDuration), 0.0f, 1.0f);
		float Scale = 1.0f;
		if (Progress < 0.7f)
		{
			const float Rise = FMath::InterpEaseOut(0.0f, 1.0f, Progress / 0.7f, 3.0f);
			Scale = FMath::Lerp(LevelUpPopStartScale, LevelUpPopOvershootScale, Rise);
		}
		else
		{
			const float Settle = FMath::InterpEaseInOut(0.0f, 1.0f, (Progress - 0.7f) / 0.3f, 2.0f);
			Scale = FMath::Lerp(LevelUpPopOvershootScale, 1.0f, Settle);
		}
		LevelUpPanel->SetRenderScale(FVector2D(Scale));
		LevelUpPanel->SetRenderOpacity(FMath::Clamp(Progress / 0.35f, 0.0f, 1.0f));
		if (Progress >= 1.0f)
		{
			LevelUpPanel->SetRenderScale(FVector2D(1.0f));
			LevelUpPanel->SetRenderOpacity(1.0f);
			bLevelUpPopPlaying = false;
		}
	}
	if (bUnlockShotPlaying && UnlockPanel)
	{
		UnlockShotElapsed += RealDelta;
		const float Duration = FMath::Max(0.1f, UnlockShotDuration);
		const float InDuration = FMath::Min(FMath::Max(0.05f, UnlockPanelSlideInDuration), Duration * 0.45f);
		const float OutDuration = FMath::Min(FMath::Max(0.05f, UnlockPanelSlideOutDuration), Duration * 0.45f);
		float VisibilityAlpha = 1.0f;
		if (UnlockShotElapsed < InDuration)
		{
			VisibilityAlpha = FMath::InterpEaseOut(0.0f, 1.0f, UnlockShotElapsed / InDuration, 3.0f);
		}
		else if (UnlockShotElapsed > Duration - OutDuration)
		{
			VisibilityAlpha = FMath::InterpEaseIn(1.0f, 0.0f,
				(UnlockShotElapsed - (Duration - OutDuration)) / OutDuration, 2.0f);
		}
		const float CachedWidth = UnlockPanel->GetCachedGeometry().GetLocalSize().X;
		const float SlideDistance = (CachedWidth > 1.0f ? CachedWidth : UnlockPanelFallbackSlideDistance) + 40.0f;
		UnlockPanel->SetRenderTranslation(FVector2D(-SlideDistance * (1.0f - FMath::Clamp(VisibilityAlpha, 0.0f, 1.0f)), 0.0f));
		UnlockPanel->SetRenderOpacity(FMath::Clamp(VisibilityAlpha, 0.0f, 1.0f));
		if (UnlockShotElapsed >= Duration) bUnlockShotPlaying = false;
	}
	UpdateCinematicVisuals();
}

void UCSLevelPresentationWidget::SetCinematicModeActive(bool bActive)
{
	if (bCinematicModeActive == bActive) return;
	bCinematicModeActive = bActive;
	LastCinematicRealTime = FPlatformTime::Seconds();
}

void UCSLevelPresentationWidget::SetSpeakerPortrait(UTexture2D* Portrait)
{
	// 빈 값은 Player/속마음 대사라는 뜻이므로 현재 장면의 인물을 그대로 유지합니다.
	if (!IsValid(Portrait) || Portrait == ActiveSpeakerPortrait) return;
	ActiveSpeakerPortrait = Portrait;
	PortraitOpacity = 0.0f;
	if (SpeakerPortraitImage)
	{
		SpeakerPortraitImage->SetBrushFromTexture(Portrait, true);
		SpeakerPortraitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UCSLevelPresentationWidget::UpdateCinematicVisuals()
{
	auto UpdateLetterbox = [this](UWidget* Bar, bool bTop)
	{
		if (!Bar) return;
		const float Height = FMath::Max(120.0f, Bar->GetCachedGeometry().GetLocalSize().Y);
		const float Direction = bTop ? -1.0f : 1.0f;
		Bar->SetRenderTranslation(FVector2D(0.0f, Direction * Height * (1.0f - LetterboxProgress)));
		Bar->SetVisibility((bCinematicModeActive || LetterboxProgress > KINDA_SMALL_NUMBER)
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	};
	UpdateLetterbox(TopLetterbox, true);
	UpdateLetterbox(BottomLetterbox, false);
	if (SpeakerPortraitImage)
	{
		SpeakerPortraitImage->SetRenderOpacity(PortraitOpacity);
		if (PortraitOpacity <= KINDA_SMALL_NUMBER && !bCinematicModeActive)
		{
			SpeakerPortraitImage->SetVisibility(ESlateVisibility::Collapsed);
			SpeakerPortraitImage->SetBrushFromTexture(nullptr);
			ActiveSpeakerPortrait = nullptr;
		}
	}
}

void UCSLevelPresentationWidget::NativeDestruct()
{
	for (UButton* Button : {StoryStartButton.Get(), DialogueNextButton.Get(), CameraNextButton.Get(), ResumeButton.Get()})
		if (Button) Button->OnClicked.RemoveDynamic(this, &ThisClass::Continue);
	for (UButton* Button : {StorySkipButton.Get(), CameraSkipButton.Get()})
		if (Button) Button->OnClicked.RemoveDynamic(this, &ThisClass::SkipScene);
	Super::NativeDestruct();
}

void UCSLevelPresentationWidget::SetLevelUpDetails(const int32 PreviousLevel, const int32 NewLevel,
	const FText& PreviousName, const FText& NewName)
{
	if (PreviousLevelText)
		PreviousLevelText->SetText(FText::Format(NSLOCTEXT("LevelPresentation", "LevelValue", "Lv. {0}"), FText::AsNumber(PreviousLevel)));
	if (NewLevelText)
		NewLevelText->SetText(FText::Format(NSLOCTEXT("LevelPresentation", "LevelValue", "Lv. {0}"), FText::AsNumber(NewLevel)));
	if (PreviousStoreNameText) PreviousStoreNameText->SetText(PreviousName);
	if (NewStoreNameText) NewStoreNameText->SetText(NewName);
}

void UCSLevelPresentationWidget::BeginUnlockShot(const float ShotDuration)
{
	UnlockShotElapsed = 0.0f;
	UnlockShotDuration = FMath::Max(0.1f, ShotDuration);
	bUnlockShotPlaying = true;
	if (UnlockPanel)
	{
		UnlockPanel->SetRenderTranslation(FVector2D(-UnlockPanelFallbackSlideDistance, 0.0f));
		UnlockPanel->SetRenderOpacity(0.0f);
	}
}

void UCSLevelPresentationWidget::Present(ECSLevelPresentationPhase Phase, ECSStorySpeaker SpeakerType,
	const FText& Heading, const FText& Body,
	const FText& Speaker, const FText& Caption, UTexture2D* Illustration,
	UTexture2D* SpeakerPortrait, bool bCameraWaiting)
{
	const bool bEnteringLevelUp = Phase == ECSLevelPresentationPhase::LevelUp &&
		(!bHasPresented || CurrentPhase != ECSLevelPresentationPhase::LevelUp);
	SetCinematicModeActive(Phase == ECSLevelPresentationPhase::Story);
	if (Phase == ECSLevelPresentationPhase::Story && SpeakerType == ECSStorySpeaker::Other)
		SetSpeakerPortrait(SpeakerPortrait);
	CurrentPhase = Phase;
	bHasPresented = true;
	DisplaySpeakerType = SpeakerType;
	bAwaitingCameraContinue = bCameraWaiting;
	DisplayHeading = Heading;
	DisplayBody = Body;
	DisplaySpeaker = Speaker;
	DisplayCaption = Caption;
	DisplayIllustration = Illustration;
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto Show = [](UWidget* Target, bool bShow) { if (Target) Target->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); };
	Show(LevelUpPanel, Phase == ECSLevelPresentationPhase::LevelUp);
	if (bEnteringLevelUp && LevelUpPanel)
	{
		LevelUpPopElapsed = 0.0f;
		bLevelUpPopPlaying = true;
		LevelUpPanel->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		LevelUpPanel->SetRenderScale(FVector2D(LevelUpPopStartScale));
		LevelUpPanel->SetRenderOpacity(0.0f);
	}
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
	const bool bOtherSpeaker = SpeakerType == ECSStorySpeaker::Other;
	const bool bPlayerSpeaker = SpeakerType == ECSStorySpeaker::Player;
	const bool bInnerSpeaker = SpeakerType == ECSStorySpeaker::PlayerThought ||
		SpeakerType == ECSStorySpeaker::Narration;
	Show(DialogueBackground_Other, Phase == ECSLevelPresentationPhase::Story && bOtherSpeaker);
	Show(DialogueBackground_Player, Phase == ECSLevelPresentationPhase::Story && bPlayerSpeaker);
	Show(DialogueBackground_Inner, Phase == ECSLevelPresentationPhase::Story && bInnerSpeaker);
	if (DialogueText)
	{
		DialogueText->SetText(Body);
		FLinearColor TextColor = NarrationTextColor;
		switch (SpeakerType)
		{
		case ECSStorySpeaker::Other: TextColor = OtherDialogueTextColor; break;
		case ECSStorySpeaker::Player: TextColor = PlayerDialogueTextColor; break;
		case ECSStorySpeaker::PlayerThought: TextColor = PlayerThoughtTextColor; break;
		case ECSStorySpeaker::Narration: TextColor = NarrationTextColor; break;
		default: break;
		}
		DialogueText->SetColorAndOpacity(FSlateColor(TextColor));
	}
	if (SpeakerText)
	{
		SpeakerText->SetText(Speaker);
		// 플레이어·속마음·내레이션은 이름표를 사용하지 않습니다.
		Show(SpeakerText, Phase == ECSLevelPresentationPhase::Story && bOtherSpeaker && !Speaker.IsEmpty());
	}
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

void UCSLevelPresentationWidget::SetStoryFadeOpacity(float Opacity)
{
	StoryFadeOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	InvalidateLayoutAndVolatility();
}

int32 UCSLevelPresentationWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled) + 1;
	if (StoryFadeOpacity > KINDA_SMALL_NUMBER)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
			AllottedGeometry.ToPaintGeometry(AllottedGeometry.GetLocalSize(), FSlateLayoutTransform()),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			FLinearColor(0.0f, 0.0f, 0.0f, StoryFadeOpacity));
	}
	return TopLayer;
}

FReply UCSLevelPresentationWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const FReply Result = HandlePresentationKeyDown(Event);
	return Result.IsEventHandled() ? Result : Super::NativeOnPreviewKeyDown(Geometry, Event);
}

FReply UCSLevelPresentationWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const FReply Result = HandlePresentationKeyDown(Event);
	return Result.IsEventHandled() ? Result : Super::NativeOnKeyDown(Geometry, Event);
}

FReply UCSLevelPresentationWidget::HandlePresentationKeyDown(const FKeyEvent& Event)
{
	// 길게 누르셔도 여러 대사가 한꺼번에 넘어가지 않도록 반복 입력을 무시합니다.
	if (Event.GetKey() == EKeys::SpaceBar || Event.GetKey() == EKeys::Enter)
	{
		const bool bCanContinue = CurrentPhase == ECSLevelPresentationPhase::LevelUp ||
			CurrentPhase == ECSLevelPresentationPhase::Story ||
			CurrentPhase == ECSLevelPresentationPhase::Goal ||
			(CurrentPhase == ECSLevelPresentationPhase::Unlock && bAwaitingCameraContinue);
		if (!Event.IsRepeat() && bCanContinue) Continue();
		return FReply::Handled();
	}
	if (Event.GetKey() == EKeys::Escape)
	{
		if (!Event.IsRepeat() && CurrentPhase == ECSLevelPresentationPhase::Unlock) SkipScene();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
