#include "UI/Store/CSWakeUpWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Curves/CurveFloat.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

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
	EyeOpenness = 0.0f;
	bFinished = false;
}

void UCSWakeUpWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bFinished) return;
	Elapsed += InDeltaTime;
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
		// 해상도에 관계없이 화면의 위아래를 덮어 중앙부터 눈이 열리도록 표현합니다.
		const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
		FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, LidHeight), FSlateLayoutTransform()),
			Brush, ESlateDrawEffect::None, FLinearColor::Black);
		FSlateDrawElement::MakeBox(OutDrawElements, TopLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, LidHeight),
				FSlateLayoutTransform(FVector2D(0.0f, Size.Y - LidHeight))),
			Brush, ESlateDrawEffect::None, FLinearColor::Black);
	}
	return TopLayer;
}
