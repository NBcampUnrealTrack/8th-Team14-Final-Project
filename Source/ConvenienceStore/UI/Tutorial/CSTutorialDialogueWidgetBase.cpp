#include "UI/Tutorial/CSTutorialDialogueWidgetBase.h"

#include "Data/CSTutorialTypes.h"
#include "Systems/Tutorial/CSTutorialSubsystem.h"

void UCSTutorialDialogueWidgetBase::NativeConstruct()
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
		this, &UCSTutorialDialogueWidgetBase::HandleStepChanged);
	TutorialSubsystem->OnTutorialCompleted.AddDynamic(
		this, &UCSTutorialDialogueWidgetBase::HandleTutorialCompleted);
}

void UCSTutorialDialogueWidgetBase::NativeDestruct()
{
	if (TutorialSubsystem != nullptr)
	{
		TutorialSubsystem->OnTutorialStepChanged.RemoveDynamic(
			this, &UCSTutorialDialogueWidgetBase::HandleStepChanged);
		TutorialSubsystem->OnTutorialCompleted.RemoveDynamic(
			this, &UCSTutorialDialogueWidgetBase::HandleTutorialCompleted);
	}

	Super::NativeDestruct();
}

void UCSTutorialDialogueWidgetBase::HandleStepChanged(const FCSTutorialStepRow& NewStep)
{
	// 조준 기반 스텝(PromptType=None)은 기존 상호작용 프롬프트에 맡기고 대화창은 띄우지 않음
	if (NewStep.PromptType != ECSTutorialPromptType::Dialogue)
	{
		OnDialogueHidden();
		return;
	}

	OnDialogueTextChanged(NewStep.PromptText);
	OnDialogueHintChanged(NewStep.PromptHintKey, NewStep.PromptHintText);
	OnDialogueHintListChanged(NewStep.PromptHintList);
}

void UCSTutorialDialogueWidgetBase::HandleTutorialCompleted()
{
	OnDialogueHidden();
}
