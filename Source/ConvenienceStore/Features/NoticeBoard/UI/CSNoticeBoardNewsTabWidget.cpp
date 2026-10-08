#include "Features/NoticeBoard/UI/CSNoticeBoardNewsTabWidget.h"

#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Systems/NoticeEvents/CSNoticeEventSubsystem.h"

void UCSNoticeBoardNewsTabWidget::NativeConstruct()
{
	Super::NativeConstruct();

	NoticeEventSubsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCSNoticeEventSubsystem>()
		: nullptr;
	if (IsValid(NoticeEventSubsystem))
	{
		NoticeEventSubsystem->OnNoticeEventsChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleNoticeEventsChanged);
	}

	RefreshNewsTab();
}

void UCSNoticeBoardNewsTabWidget::NativeDestruct()
{
	if (IsValid(NoticeEventSubsystem))
	{
		NoticeEventSubsystem->OnNoticeEventsChanged.RemoveDynamic(
			this,
			&ThisClass::HandleNoticeEventsChanged);
	}
	NoticeEventSubsystem = nullptr;
	Super::NativeDestruct();
}

void UCSNoticeBoardNewsTabWidget::RefreshNewsTab()
{
	FCSNewsEventDataRow News;
	TArray<FCSNoticeBoardDemandChange> Changes;
	const bool bHasNews = IsValid(NoticeEventSubsystem) && NoticeEventSubsystem->GetTodayNews(News);
	// 뉴스가 없는 기념일에도 오르는 상품을 보여줘야 해서 뉴스 유무와 관계없이 받는다
	if (IsValid(NoticeEventSubsystem))
	{
		NoticeEventSubsystem->GetTodayNewsDemandChanges(Changes);
	}

	RefreshNewsHeaderView(News, bHasNews);
	PopulateDemandRows(Changes);
}

void UCSNoticeBoardNewsTabWidget::HandleNoticeEventsChanged()
{
	RefreshNewsTab();
}

UScrollBox* UCSNoticeBoardNewsTabWidget::ResolveDemandScrollBox() const
{
	if (IsValid(ScrollBox_DemandChanges))
	{
		return ScrollBox_DemandChanges;
	}

	return Cast<UScrollBox>(GetWidgetFromName(TEXT("ScrollBox_63")));
}

TSubclassOf<UUserWidget> UCSNoticeBoardNewsTabWidget::ResolveDemandRowClass() const
{
	if (DemandRowWidgetClass)
	{
		return DemandRowWidgetClass;
	}

	return LoadClass<UUserWidget>(
		nullptr,
		TEXT("/Game/ConvenienceStore/UI/NoticeBoard/WBP_NoticeBoardNewsDemandRow.WBP_NoticeBoardNewsDemandRow_C"));
}

void UCSNoticeBoardNewsTabWidget::PopulateDemandRows(
	const TArray<FCSNoticeBoardDemandChange>& Changes)
{
	UScrollBox* DemandScrollBox = ResolveDemandScrollBox();
	const TSubclassOf<UUserWidget> RowClass = ResolveDemandRowClass();
	if (!IsValid(DemandScrollBox))
	{
		return;
	}

	// 디자이너에 넣어둔 미리보기 Row도 실행 시점에는 실제 데이터로 교체합니다.
	DemandScrollBox->ClearChildren();
	if (!RowClass)
	{
		return;
	}

	for (const FCSNoticeBoardDemandChange& Change : Changes)
	{
		UUserWidget* RowWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), RowClass);
		if (!IsValid(RowWidget))
		{
			continue;
		}

		SetupDemandRow(RowWidget, Change);
		DemandScrollBox->AddChild(RowWidget);
	}
}

void UCSNoticeBoardNewsTabWidget::SetupDemandRow(
	UUserWidget* RowWidget,
	const FCSNoticeBoardDemandChange& Change) const
{
	if (!IsValid(RowWidget))
	{
		return;
	}

	if (UTextBlock* TargetText = Cast<UTextBlock>(RowWidget->GetWidgetFromName(TEXT("Text_TargetName"))))
	{
		TargetText->SetText(Change.TargetDisplayName);
	}
	if (UTextBlock* DirectionText = Cast<UTextBlock>(RowWidget->GetWidgetFromName(TEXT("Text_DemandDirection"))))
	{
		const bool bHoliday = Change.SourceType == ECSNoticeEventSourceType::Holiday;
		DirectionText->SetText(Change.bIsIncrease
			? (bHoliday ? NSLOCTEXT("NoticeBoard", "HolidayDemandIncrease", "기념일 수요 상승")
						: NSLOCTEXT("NoticeBoard", "DemandIncrease", "수요 상승"))
			: (bHoliday ? NSLOCTEXT("NoticeBoard", "HolidayDemandDecrease", "기념일 수요 하락")
						: NSLOCTEXT("NoticeBoard", "DemandDecrease", "수요 하락")));
	}
	if (UImage* Background = Cast<UImage>(RowWidget->GetWidgetFromName(TEXT("Image_DemandBackground"))))
	{
		const TCHAR* TexturePath = Change.bIsIncrease
			? TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_newsup.T_Board_newsup")
			: TEXT("/Game/ConvenienceStore/UI/NoticeBoard/Texture/T_Board_newsdown.T_Board_newsdown");
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TexturePath))
		{
			Background->SetBrushFromTexture(Texture, false);
		}
	}
}
