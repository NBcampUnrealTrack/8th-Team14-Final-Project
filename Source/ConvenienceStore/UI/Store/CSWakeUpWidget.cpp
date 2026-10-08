#include "UI/Store/CSWakeUpWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Curves/CurveFloat.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformTime.h"

TSharedRef<SWidget> UCSWakeUpWidget::RebuildWidget()
{
	// 별도 위젯 에셋을 지정하지 않으셔도 전체 화면에서 연출이 동작하도록 구성합니다.
	if (!WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	}
	return Super::RebuildWidget();
}

void UCSWakeUpWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Elapsed = 0.0f;
	LastRealTime = FPlatformTime::Seconds();
	EyeOpenness = 0.0f;
	bFinished = false;
}

void UCSWakeUpWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bFinished) return;
	// 게임이 정지된 오프닝에서도 눈뜨기가 진행되도록 실제 시간을 사용합니다.
	const double Now = FPlatformTime::Seconds();
	Elapsed += FMath::Clamp(static_cast<float>(Now - LastRealTime), 0.0f, 0.1f);
	LastRealTime = Now;
	const float Progress = FMath::Clamp(Elapsed / FMath::Max(0.1f, EyeOpeningDuration), 0.0f, 1.0f);
	if (IsValid(EyeOpeningCurve))
	{
		EyeOpenness = FMath::Clamp(EyeOpeningCurve->GetFloatValue(Progress), 0.0f, 1.0f);
	}
	else if (Progress < 0.3f)
	{
		EyeOpenness = 0.22f * FMath::SmoothStep(0.0f, 0.3f, Progress);
	}
	else if (Progress < 0.45f)
	{
		EyeOpenness = 0.22f * (1.0f - FMath::SmoothStep(0.3f, 0.45f, Progress));
	}
	else
	{
		EyeOpenness = FMath::SmoothStep(0.45f, 1.0f, Progress);
	}
	InvalidateLayoutAndVolatility();
	if (Progress >= 1.0f)
	{
		EyeOpenness = 1.0f;
		bFinished = true;
		OnEyeOpeningFinished.Broadcast();
	}
}

int32 UCSWakeUpWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled) + 1;
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float LidHeight = Size.Y * 0.5f * (1.0f - EyeOpenness);
	if (LidHeight > 0.0f)
	{
		// 해상도에 관계없이 중앙은 진하고 화면 바깥은 옅은 눈꺼풀 그라데이션을 그립니다.
		const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
		const int32 StepCount = FMath::Clamp(LidGradientSteps, 2, 32);
		const float StepHeight = LidHeight / StepCount;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const float GradientAlpha = FMath::SmoothStep(0.0f, 1.0f,
				(static_cast<float>(StepIndex) + 0.5f) / StepCount);
			const FLinearColor StepColor = FMath::Lerp(OuterLidColor, CenterLidColor, GradientAlpha);

			// 위 눈꺼풀은 화면 위에서 중앙으로, 아래 눈꺼풀은 화면 아래에서 중앙으로 진해집니다.
			FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
				AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, StepHeight),
					FSlateLayoutTransform(FVector2D(0.0f, StepIndex * StepHeight))),
				Brush, ESlateDrawEffect::None, StepColor);
			FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
				AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, StepHeight),
					FSlateLayoutTransform(FVector2D(0.0f, Size.Y - (StepIndex + 1) * StepHeight))),
				Brush, ESlateDrawEffect::None, StepColor);
		}
	}
	return TopLayer;
}
