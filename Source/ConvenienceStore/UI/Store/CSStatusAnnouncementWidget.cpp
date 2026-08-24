#include "UI/Store/CSStatusAnnouncementWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Components/TextBlock.h"

void UCSStatusAnnouncementWidget::NativeConstruct()
{
	Super::NativeConstruct();

	FWidgetAnimationDynamicEvent FinishedEvent;
	FinishedEvent.BindDynamic(
		this,
		&UCSStatusAnnouncementWidget::HandleAnnouncementAnimationFinished);
	BindToAnimationFinished(AnnouncementAnimation, FinishedEvent);
}

void UCSStatusAnnouncementWidget::NativeDestruct()
{
	UnbindAllFromAnimationFinished(AnnouncementAnimation);
	Super::NativeDestruct();
}

void UCSStatusAnnouncementWidget::PlayAnnouncement(
	ECSStatusAnnouncementType InAnnouncementType)
{
	AnnouncementType = InAnnouncementType;
	bIsAnnouncementPlaying = true;

	if (IsValid(AnnouncementText))
	{
		AnnouncementText->SetText(
			AnnouncementType == ECSStatusAnnouncementType::Opened
				? OpenedAnnouncementText
				: ClosedAnnouncementText);
	}

	PlayAnimation(AnnouncementAnimation);
}

void UCSStatusAnnouncementWidget::NotifyAnnouncementFinished()
{
	// 애니메이션 이벤트가 중복 배치되어도 완료 이벤트는 한 번만 전달합니다.
	if (!bIsAnnouncementPlaying)
	{
		return;
	}

	bIsAnnouncementPlaying = false;
	OnAnnouncementFinished.Broadcast();
}

void UCSStatusAnnouncementWidget::HandleAnnouncementAnimationFinished()
{
	NotifyAnnouncementFinished();
}
