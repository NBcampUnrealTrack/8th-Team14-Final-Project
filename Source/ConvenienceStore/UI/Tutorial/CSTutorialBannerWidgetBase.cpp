#include "UI/Tutorial/CSTutorialBannerWidgetBase.h"

#include "Data/CSTutorialTypes.h"
#include "Systems/Tutorial/CSTutorialSubsystem.h"

// 앞 배너가 사라지는 애니메이션이 끝난 뒤 다음 배너가 나오게 두는 간격
static constexpr float TipBannerGapSeconds = 0.5f;

void UCSTutorialBannerWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	TutorialSubsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCSTutorialSubsystem>()
		: nullptr;

	if (TutorialSubsystem == nullptr)
	{
		return;
	}

	TutorialSubsystem->OnTutorialStepChanged.AddDynamic(
		this, &UCSTutorialBannerWidgetBase::HandleStepChanged);
	TutorialSubsystem->OnTutorialCompleted.AddDynamic(
		this, &UCSTutorialBannerWidgetBase::HandleTutorialCompleted);
}

void UCSTutorialBannerWidgetBase::NativeDestruct()
{
	if (TutorialSubsystem != nullptr)
	{
		TutorialSubsystem->OnTutorialStepChanged.RemoveDynamic(
			this, &UCSTutorialBannerWidgetBase::HandleStepChanged);
		TutorialSubsystem->OnTutorialCompleted.RemoveDynamic(
			this, &UCSTutorialBannerWidgetBase::HandleTutorialCompleted);
	}

	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(BannerHideTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(TipBannerGapTimerHandle);
	}

	Super::NativeDestruct();
}

void UCSTutorialBannerWidgetBase::HandleStepChanged(const FCSTutorialStepRow& NewStep)
{
	// 이 스텝에 배너 문구가 없으면 배너를 띄우지 않음
	if (NewStep.BannerText.IsEmpty())
	{
		// 대화창만 바꾸는 방송에 떠 있던 팁 배너가 같이 지워지지 않게 둔다
		if (!bTipBannerShowing)
		{
			if (GetWorld() != nullptr)
			{
				GetWorld()->GetTimerManager().ClearTimer(BannerHideTimerHandle);
			}
			OnBannerHidden();
		}
		return;
	}

	// 스텝 배너가 우선이라 기다리던 팁 배너는 버린다
	PendingTipBanners.Reset();
	bTipBannerShowing = false;
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(TipBannerGapTimerHandle);
	}
	DisplayBanner(NewStep.BannerText);
}

void UCSTutorialBannerWidgetBase::ShowTipBanner(const FText& Text)
{
	const bool bWaitingForGap = GetWorld() != nullptr &&
		GetWorld()->GetTimerManager().IsTimerActive(TipBannerGapTimerHandle);
	if (bTipBannerShowing || bWaitingForGap)
	{
		PendingTipBanners.Add(Text);
		return;
	}

	bTipBannerShowing = true;
	DisplayBanner(Text);
}

void UCSTutorialBannerWidgetBase::DisplayBanner(const FText& Text)
{
	OnBannerTextChanged(Text);

	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().SetTimer(
			BannerHideTimerHandle,
			this,
			&UCSTutorialBannerWidgetBase::HideBanner,
			BannerDisplaySeconds,
			false);
	}
}

void UCSTutorialBannerWidgetBase::ShowNextTipBanner()
{
	if (PendingTipBanners.IsEmpty())
	{
		return;
	}

	const FText Text = PendingTipBanners[0];
	PendingTipBanners.RemoveAt(0);
	bTipBannerShowing = true;
	DisplayBanner(Text);
}

void UCSTutorialBannerWidgetBase::HandleTutorialCompleted()
{
	PendingTipBanners.Reset();
	HideBanner();
}

void UCSTutorialBannerWidgetBase::HideBanner()
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(BannerHideTimerHandle);
	}
	OnBannerHidden();
	bTipBannerShowing = false;

	if (!PendingTipBanners.IsEmpty() && GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().SetTimer(
			TipBannerGapTimerHandle,
			this,
			&UCSTutorialBannerWidgetBase::ShowNextTipBanner,
			TipBannerGapSeconds,
			false);
	}
}
