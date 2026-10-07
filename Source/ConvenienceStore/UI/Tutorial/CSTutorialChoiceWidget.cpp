#include "UI/Tutorial/CSTutorialChoiceWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UCSTutorialChoiceWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	YesButton->OnClicked.AddUniqueDynamic(this, &UCSTutorialChoiceWidget::HandleYesClicked);
	NoButton->OnClicked.AddUniqueDynamic(this, &UCSTutorialChoiceWidget::HandleNoClicked);
}

void UCSTutorialChoiceWidget::NativeDestruct()
{
	YesButton->OnClicked.RemoveDynamic(this, &UCSTutorialChoiceWidget::HandleYesClicked);
	NoButton->OnClicked.RemoveDynamic(this, &UCSTutorialChoiceWidget::HandleNoClicked);
	Super::NativeDestruct();
}

void UCSTutorialChoiceWidget::SetTexts(const FText& Question, const FText& YesLabel, const FText& NoLabel)
{
	QuestionText->SetText(Question);
	YesText->SetText(YesLabel);
	NoText->SetText(NoLabel);
}

// 둘 중 하나를 골라야 다음으로 갈 수 있어서 Esc로 닫히지 않게 먼저 삼킨다
FReply UCSTutorialChoiceWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UCSTutorialChoiceWidget::HandleYesClicked()
{
	OnChoiceMade.Broadcast(true);
}

void UCSTutorialChoiceWidget::HandleNoClicked()
{
	OnChoiceMade.Broadcast(false);
}
