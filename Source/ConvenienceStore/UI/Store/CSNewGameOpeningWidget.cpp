#include "UI/Store/CSNewGameOpeningWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

UCSNewGameOpeningWidget::UCSNewGameOpeningWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoStartEyeOpening = false;
	static ConstructorHelpers::FClassFinder<UCSWakeUpWidget> WakeUpTemplate(
		TEXT("/Game/ConvenienceStore/UI/WakeUp/WBP_WakeUpWidget"));
	if (WakeUpTemplate.Succeeded()) EyeAppearanceTemplateClass = WakeUpTemplate.Class;
}

TSharedRef<SWidget> UCSNewGameOpeningWidget::RebuildWidget()
{
	TSharedRef<SWidget> Result = Super::RebuildWidget();
	if (!OpeningIllustrationImage)
	{
		if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
		{
			OpeningIllustrationImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OpeningIllustrationImage"));
			UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(OpeningIllustrationImage);
			CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			CanvasSlot->SetOffsets(FMargin(0.0f));
		}
	}
	return Result;
}

void UCSNewGameOpeningWidget::NativeConstruct()
{
	ApplyEyeAppearanceTemplate();
	Super::NativeConstruct();
	if (OpeningIllustrationImage)
	{
		OpeningIllustrationImage->SetBrushFromTexture(OpeningIllustration, true);
		OpeningIllustrationImage->SetOpacity(0.0f);
		OpeningIllustrationImage->SetVisibility(OpeningIllustration ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	OpeningStage = EOpeningStage::InitialFadeOut;
	StageElapsed = 0.0f;
	// 위젯이 붙는 첫 프레임부터 게임 화면을 완전히 가린 상태로 시작합니다.
	BlackOpacity = 1.0f;
	LastOpeningRealTime = FPlatformTime::Seconds();
}

void UCSNewGameOpeningWidget::ApplyEyeAppearanceTemplate()
{
	const UCSWakeUpWidget* Template = EyeAppearanceTemplateClass
		? EyeAppearanceTemplateClass->GetDefaultObject<UCSWakeUpWidget>() : nullptr;
	if (!Template || Template == this) return;
	CopyEyeAppearanceFrom(*Template);
}

void UCSNewGameOpeningWidget::NativeDestruct()
{
	StopOpeningAudio();
	Super::NativeDestruct();
}

void UCSNewGameOpeningWidget::AdvanceStage(EOpeningStage NewStage)
{
	OpeningStage = NewStage;
	StageElapsed = 0.0f;
	if (NewStage == EOpeningStage::IllustrationFadeIn && OpeningIllustrationImage)
		OpeningIllustrationImage->SetOpacity(1.0f);
	else if (NewStage == EOpeningStage::FirstAudio)
		PlayOpeningAudio(FirstOpeningAudio);
	else if (NewStage == EOpeningStage::SecondAudio)
		PlayOpeningAudio(SecondOpeningAudio);
	else if (NewStage == EOpeningStage::EyeOpening)
	{
		StopOpeningAudio();
		if (OpeningIllustrationImage) OpeningIllustrationImage->SetVisibility(ESlateVisibility::Collapsed);
		BlackOpacity = 0.0f;
		StartEyeOpening();
	}
}

void UCSNewGameOpeningWidget::PlayOpeningAudio(USoundBase* Sound)
{
	StopOpeningAudio();
	if (!Sound || !GetWorld()) return;
	ActiveAudioComponent = UGameplayStatics::CreateSound2D(
		GetWorld(), Sound, FMath::Clamp(OpeningAudioVolumeMultiplier, 0.0f, 4.0f),
		1.0f, 0.0f, nullptr, false, false);
	if (ActiveAudioComponent)
	{
		ActiveAudioComponent->SetUISound(true);
		ActiveAudioComponent->Play();
	}
}

void UCSNewGameOpeningWidget::StopOpeningAudio()
{
	if (!ActiveAudioComponent) return;
	ActiveAudioComponent->Stop();
	ActiveAudioComponent->DestroyComponent();
	ActiveAudioComponent = nullptr;
}

void UCSNewGameOpeningWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (OpeningStage == EOpeningStage::EyeOpening) return;
	const double Now = FPlatformTime::Seconds();
	StageElapsed += FMath::Clamp(static_cast<float>(Now - LastOpeningRealTime), 0.0f, 0.1f);
	LastOpeningRealTime = Now;

	switch (OpeningStage)
	{
	case EOpeningStage::InitialFadeOut:
		// 게임 화면이 한 프레임이라도 비치지 않도록 암전을 유지합니다.
		BlackOpacity = 1.0f;
		if (StageElapsed >= InitialFadeOutDuration) AdvanceStage(EOpeningStage::IllustrationFadeIn);
		break;
	case EOpeningStage::IllustrationFadeIn:
		BlackOpacity = 1.0f - FMath::SmoothStep(0.0f, FMath::Max(0.05f, IllustrationFadeInDuration), StageElapsed);
		if (StageElapsed >= IllustrationFadeInDuration) { BlackOpacity = 0.0f; AdvanceStage(EOpeningStage::BeforeFirstAudio); }
		break;
	case EOpeningStage::BeforeFirstAudio:
		if (StageElapsed >= FirstAudioDelay) AdvanceStage(EOpeningStage::FirstAudio);
		break;
	case EOpeningStage::FirstAudio:
		if (StageElapsed >= FirstAudioDuration) AdvanceStage(EOpeningStage::SecondAudio);
		break;
	case EOpeningStage::SecondAudio:
		BlackOpacity = FMath::SmoothStep(0.0f, FMath::Min(FMath::Max(0.05f, FinalFadeOutDuration), FMath::Max(0.05f, SecondAudioDuration)), StageElapsed);
		if (StageElapsed >= SecondAudioDuration) { BlackOpacity = 1.0f; AdvanceStage(EOpeningStage::EyeOpening); }
		break;
	default: break;
	}
	InvalidateLayoutAndVolatility();
}

int32 UCSNewGameOpeningWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled) + 1;
	if (BlackOpacity > KINDA_SMALL_NUMBER)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
			AllottedGeometry.ToPaintGeometry(AllottedGeometry.GetLocalSize(), FSlateLayoutTransform()),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			FLinearColor(0.0f, 0.0f, 0.0f, BlackOpacity));
	}
	return TopLayer;
}
