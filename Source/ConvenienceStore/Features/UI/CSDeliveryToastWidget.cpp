#include "Features/UI/CSDeliveryToastWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Components/TextBlock.h"
#include "TimerManager.h"

void UCSDeliveryToastWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(ToastAnimation))
	{
		FWidgetAnimationDynamicEvent FinishedEvent;
		FinishedEvent.BindDynamic(this, &ThisClass::HandleToastAnimationFinished);
		BindToAnimationFinished(ToastAnimation, FinishedEvent);
	}
}

void UCSDeliveryToastWidget::NativeDestruct()
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(FallbackTimerHandle);
	}
	if (IsValid(ToastAnimation))
	{
		UnbindAllFromAnimationFinished(ToastAnimation);
	}

	Super::NativeDestruct();
}

void UCSDeliveryToastWidget::ShowToast(const FText& InTitle, const FText& InMessage)
{
	if (IsValid(Text_Title)) Text_Title->SetText(InTitle);
	if (IsValid(Text_Message)) Text_Message->SetText(InMessage);

	bIsToastPlaying = true;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(FallbackTimerHandle);
	}

	if (IsValid(ToastAnimation))
	{
		StopAnimation(ToastAnimation);
		PlayAnimation(ToastAnimation);
		return;
	}

	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().SetTimer(
			FallbackTimerHandle, this, &ThisClass::FinishToast, FallbackDurationSeconds, false);
	}
}

void UCSDeliveryToastWidget::HandleToastAnimationFinished()
{
	FinishToast();
}

void UCSDeliveryToastWidget::FinishToast()
{
	if (!bIsToastPlaying)
	{
		return;
	}

	bIsToastPlaying = false;
	OnToastFinished.Broadcast();
}
