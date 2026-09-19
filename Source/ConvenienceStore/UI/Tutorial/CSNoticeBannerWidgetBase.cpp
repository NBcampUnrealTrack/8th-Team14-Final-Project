#include "UI/Tutorial/CSNoticeBannerWidgetBase.h"

void UCSNoticeBannerWidgetBase::NativeDestruct()
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(BannerHideTimerHandle);
	}

	Super::NativeDestruct();
}

void UCSNoticeBannerWidgetBase::ShowMessage(const FText& Message)
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(BannerHideTimerHandle);
	}

	OnBannerTextChanged(Message);

	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().SetTimer(
			BannerHideTimerHandle,
			this,
			&UCSNoticeBannerWidgetBase::HideBanner,
			BannerDisplaySeconds,
			false);
	}
}

void UCSNoticeBannerWidgetBase::HideBanner()
{
	OnBannerHidden();
}
