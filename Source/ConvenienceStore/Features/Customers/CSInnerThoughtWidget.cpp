#include "Features/Customers/CSInnerThoughtWidget.h"
#include "Components/TextBlock.h"

void UCSInnerThoughtWidget::SetThoughtText(const FText& InText)
{
	if (IsValid(ThoughtText))
	{
		ThoughtText->SetText(InText);
	}
}

void UCSInnerThoughtWidget::ShowAnimated()
{
	SetVisibility(ESlateVisibility::Visible);
	TransitionState = ETransitionState::Showing;
	TransitionElapsed = 0.0f;
	SetRenderOpacity(0.0f);
	SetRenderTranslation(FVector2D(0.0f, AnimationVerticalOffset));
}

void UCSInnerThoughtWidget::HideAnimated()
{
	if (GetVisibility() == ESlateVisibility::Collapsed)
	{
		return;
	}

	TransitionState = ETransitionState::Hiding;
	TransitionElapsed = 0.0f;
}

void UCSInnerThoughtWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (TransitionState == ETransitionState::None)
	{
		return;
	}

	TransitionElapsed += InDeltaTime;
	const float Duration = TransitionState == ETransitionState::Showing
		? ShowAnimationDuration
		: HideAnimationDuration;
	const float Alpha = FMath::Clamp(TransitionElapsed / FMath::Max(Duration, UE_SMALL_NUMBER), 0.0f, 1.0f);
	const float EasedAlpha = FMath::InterpEaseOut(0.0f, 1.0f, Alpha, 3.0f);

	if (TransitionState == ETransitionState::Showing)
	{
		SetRenderOpacity(EasedAlpha);
		SetRenderTranslation(FVector2D(0.0f, AnimationVerticalOffset * (1.0f - EasedAlpha)));
	}
	else
	{
		SetRenderOpacity(1.0f - EasedAlpha);
		SetRenderTranslation(FVector2D(0.0f, AnimationVerticalOffset * EasedAlpha));
	}

	if (Alpha < 1.0f)
	{
		return;
	}

	if (TransitionState == ETransitionState::Hiding)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		SetRenderOpacity(1.0f);
		SetRenderTranslation(FVector2D::ZeroVector);
	}
	TransitionState = ETransitionState::None;
}
