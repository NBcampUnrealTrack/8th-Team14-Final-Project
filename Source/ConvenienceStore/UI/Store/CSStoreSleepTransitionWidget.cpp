#include "UI/Store/CSStoreSleepTransitionWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"

void UCSStoreSleepTransitionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	SetRenderOpacity(0.0f);

	NextDayButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleNextDayClicked);
	FinalSummaryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleFinalSummaryClicked);

	PlayAnimation(SleepZLoop, 0.0f, 0, EUMGSequencePlayMode::Forward, 1.0f, false);
	FadeElapsed = 0.0f;
	bFadingIn = true;
	bFadingOut = false;
	bNextDayBroadcast = false;
}

void UCSStoreSleepTransitionWidget::NativeDestruct()
{
	NextDayButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleNextDayClicked);
	FinalSummaryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleFinalSummaryClicked);
	Super::NativeDestruct();
}

void UCSStoreSleepTransitionWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bFadingIn && !bFadingOut) return;

	FadeElapsed += InDeltaTime;
	const float Alpha = FMath::Clamp(FadeElapsed / FMath::Max(0.01f, WidgetFadeDuration), 0.0f, 1.0f);
	SetRenderOpacity(bFadingOut ? 1.0f - Alpha : Alpha);

	if (Alpha < 1.0f) return;
	if (bFadingIn)
	{
		bFadingIn = false;
		return;
	}

	bFadingOut = false;
	if (!bNextDayBroadcast)
	{
		bNextDayBroadcast = true;
		OnNextDayRequested.Broadcast();
	}
}

void UCSStoreSleepTransitionWidget::HandleNextDayClicked()
{
	if (bFadingOut || bNextDayBroadcast) return;
	NextDayButton->SetIsEnabled(false);
	FinalSummaryButton->SetIsEnabled(false);
	FadeElapsed = 0.0f;
	bFadingIn = false;
	bFadingOut = true;
}

void UCSStoreSleepTransitionWidget::HandleFinalSummaryClicked()
{
	if (!bFadingOut) OnSummaryRequested.Broadcast();
}
