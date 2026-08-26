#include "UI/Store/CSStoreBusinessConfirmWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UCSStoreBusinessConfirmWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(ConfirmButton))
	{
		ConfirmButton->OnClicked.AddDynamic(this, &UCSStoreBusinessConfirmWidget::HandleConfirmClicked);
	}
	if (IsValid(CancelButton))
	{
		CancelButton->OnClicked.AddDynamic(this, &UCSStoreBusinessConfirmWidget::HandleCancelClicked);
	}

	SetConfirmationType(ConfirmationType);
}

void UCSStoreBusinessConfirmWidget::SetConfirmationType(
	ECSStoreBusinessConfirmType InConfirmationType)
{
	ConfirmationType = InConfirmationType;
	bShouldOpen = ConfirmationType != ECSStoreBusinessConfirmType::PauseEntry;

	FText Title;
	FText Question;
	FText Guide;
	FText ConfirmLabel;

	switch (ConfirmationType)
	{
	case ECSStoreBusinessConfirmType::PauseEntry:
		Title = NSLOCTEXT("Store", "PauseCustomerEntryTitle", "고객 입장 마감");
		Question = NSLOCTEXT("Store", "PauseCustomerEntryQuestion",
			"새로운 고객의 방문을 잠시 중단하시겠습니까?");
		Guide = NSLOCTEXT("Store", "PauseCustomerEntryGuide",
			"매장에 있는 고객의 쇼핑과 계산은 계속 진행됩니다.");
		ConfirmLabel = NSLOCTEXT("Store", "PauseCustomerEntryButton", "입장 마감");
		break;

	case ECSStoreBusinessConfirmType::ResumeEntry:
		Title = NSLOCTEXT("Store", "ResumeCustomerEntryTitle", "고객 입장 재개");
		Question = NSLOCTEXT("Store", "ResumeCustomerEntryQuestion",
			"새로운 고객의 입장을 다시 받으시겠습니까?");
		Guide = NSLOCTEXT("Store", "ResumeCustomerEntryGuide",
			"입간판을 OPEN으로 전환하면 고객 방문이 재개됩니다.");
		ConfirmLabel = NSLOCTEXT("Store", "ResumeCustomerEntryButton", "입장 재개");
		break;

	case ECSStoreBusinessConfirmType::InitialOpen:
	default:
		Title = NSLOCTEXT("Store", "InitialBusinessOpenTitle", "영업 시작");
		Question = NSLOCTEXT("Store", "InitialBusinessOpenQuestion",
			"오늘의 영업을 시작하시겠습니까?");
		Guide = NSLOCTEXT("Store", "InitialBusinessOpenGuide",
			"입간판을 OPEN으로 전환하고 고객을 맞이합니다.");
		ConfirmLabel = NSLOCTEXT("Store", "InitialBusinessOpenButton", "영업 시작");
		break;
	}

	if (IsValid(TitleText))
	{
		TitleText->SetText(Title);
	}
	if (IsValid(QuestionText))
	{
		QuestionText->SetText(Question);
	}
	if (IsValid(GuideText))
	{
		GuideText->SetText(Guide);
	}
	if (IsValid(ConfirmButtonText))
	{
		ConfirmButtonText->SetText(ConfirmLabel);
	}

	// 기존 단일 텍스트 블록을 사용하는 위젯도 새 문구로 동작하도록 호환성을 유지합니다.
	if (IsValid(MessageText))
	{
		MessageText->SetText(FText::Format(
			NSLOCTEXT("Store", "BusinessConfirmLegacyFormat", "{0}\n{1}\n{2}"),
			Title,
			Question,
			Guide));
	}
}

void UCSStoreBusinessConfirmWidget::HandleConfirmClicked()
{
	OnBusinessConfirmed.Broadcast(bShouldOpen);
}

void UCSStoreBusinessConfirmWidget::HandleCancelClicked()
{
	OnBusinessCancelled.Broadcast();
}
