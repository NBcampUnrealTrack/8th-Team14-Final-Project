#include "Features/NoticeBoard/UI/CSNoticeBoardWidget.h"

#include "Features/NoticeBoard/CSNoticeBoardActor.h"

void UCSNoticeBoardWidget::InitializeNoticeBoard(ACSNoticeBoardActor* InNoticeBoardActor)
{
	NoticeBoardActor = InNoticeBoardActor;
	if (!IsValid(NoticeBoardActor))
	{
		return;
	}

	InitializeNoticeBoardView();
	ShowNoticeBoardTab(SelectedTabId);
}

void UCSNoticeBoardWidget::CloseNoticeBoardWindow()
{
	if (IsValid(NoticeBoardActor))
	{
		NoticeBoardActor->CloseNoticeBoardUI(this);
	}
}

void UCSNoticeBoardWidget::SelectTab(const FName TabId)
{
	if (TabId.IsNone())
	{
		return;
	}

	SelectedTabId = TabId;
	ShowNoticeBoardTab(SelectedTabId);
}

void UCSNoticeBoardWidget::NativeDestruct()
{
	// 외부에서 RemoveFromParent를 호출한 경우에도 액터가 오래된 위젯을 보관하지 않게 합니다.
	if (IsValid(NoticeBoardActor))
	{
		NoticeBoardActor->CloseNoticeBoardUI(this);
	}
	Super::NativeDestruct();
}
