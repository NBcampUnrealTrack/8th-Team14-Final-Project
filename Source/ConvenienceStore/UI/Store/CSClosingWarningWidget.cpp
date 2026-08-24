#include "UI/Store/CSClosingWarningWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Components/TextBlock.h"

void UCSClosingWarningWidget::NativeConstruct()
{
	Super::NativeConstruct();

	FWidgetAnimationDynamicEvent FinishedEvent;
	FinishedEvent.BindDynamic(
		this,
		&UCSClosingWarningWidget::HandleWarningAnimationFinished);
	BindToAnimationFinished(WarningAnimation, FinishedEvent);
}

void UCSClosingWarningWidget::NativeDestruct()
{
	UnbindAllFromAnimationFinished(WarningAnimation);
	Super::NativeDestruct();
}

void UCSClosingWarningWidget::ShowWarning(int32 RemainingMinutes)
{
	if (IsValid(WarningText))
	{
		WarningText->SetText(FText::Format(
			NSLOCTEXT(
				"StoreClosingWarning",
				"ClosingMinutesRemaining",
				"영업 종료까지 {0}분 남았습니다"),
			FText::AsNumber(FMath::Max(0, RemainingMinutes))));
	}

	bIsWarningPlaying = true;
	StopAnimation(WarningAnimation);
	PlayAnimation(WarningAnimation);
}

void UCSClosingWarningWidget::HandleWarningAnimationFinished()
{
	if (!bIsWarningPlaying)
	{
		return;
	}

	bIsWarningPlaying = false;
	OnWarningFinished.Broadcast();
}
