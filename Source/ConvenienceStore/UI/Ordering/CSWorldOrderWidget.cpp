#include "UI/Ordering/CSWorldOrderWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Features/Ordering/CSOrderWorldWidgetComponent.h"

void UCSWorldOrderWidget::OpenInWorld(UCSOrderWorldWidgetComponent* InHost)
{
	WorldHost = InHost;
	bClosing = false;
	bCompletingClose = false;
	bOpening = true;
	SetVisibility(ESlateVisibility::Visible);
	SetIsEnabled(false);
	PlayAnimation(Anim_OpenOrder);
}

bool UCSWorldOrderWidget::DeferCloseForWorldAnimation()
{
	if (!WorldHost || bCompletingClose)
	{
		return false;
	}
	if (bOpening || bClosing)
	{
		return true;
	}
	bClosing = true;
	SetIsEnabled(false);
	PlayAnimationReverse(Anim_OpenOrder);
	return true;
}

void UCSWorldOrderWidget::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{
	Super::OnAnimationFinished_Implementation(Animation);
	if (Animation != Anim_OpenOrder)
	{
		return;
	}
	if (bClosing)
	{
		if (WorldHost)
		{
			WorldHost->FinishPresentation();
		}
		else
		{
			CompleteCloseAfterCameraReturn();
		}
	}
	else if (bOpening)
	{
		bOpening = false;
		SetIsEnabled(true);
		SetKeyboardFocus();
	}
}

void UCSWorldOrderWidget::CompleteCloseAfterCameraReturn()
{
	bCompletingClose = true;
	CompleteWorldOrderClose();
	bOpening = false;
	bClosing = false;
	bCompletingClose = false;
	WorldHost = nullptr;
	RemoveFromParent();
}

void UCSWorldOrderWidget::NativeDestruct()
{
	if (WorldHost)
	{
		WorldHost->CancelPresentation();
	}
	Super::NativeDestruct();
}
