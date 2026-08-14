#include "UI/Tutorial/CSTutorialBannerWidgetBase.h"

#include "Data/CSTutorialTypes.h"
#include "Systems/Tutorial/CSTutorialSubsystem.h"

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
	}

	Super::NativeDestruct();
}

void UCSTutorialBannerWidgetBase::HandleStepChanged(const FCSTutorialStepRow& NewStep)
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(BannerHideTimerHandle);
	}

	// 이 스텝에 배너 문구가 없으면 배너를 띄우지 않음
	if (NewStep.BannerText.IsEmpty())
	{
		OnBannerHidden();
		return;
	}

	OnBannerTextChanged(NewStep.BannerText);

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

void UCSTutorialBannerWidgetBase::HandleTutorialCompleted()
{
	HideBanner();
}

void UCSTutorialBannerWidgetBase::HideBanner()
{
	OnBannerHidden();
}
