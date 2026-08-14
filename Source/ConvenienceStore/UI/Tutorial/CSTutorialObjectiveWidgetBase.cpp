#include "UI/Tutorial/CSTutorialObjectiveWidgetBase.h"
#include "Data/CSTutorialTypes.h"
#include "Systems/Tutorial/CSTutorialSubsystem.h"

void UCSTutorialObjectiveWidgetBase::NativeConstruct()
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
		this, &UCSTutorialObjectiveWidgetBase::HandleStepChanged);
	TutorialSubsystem->OnTutorialObjectiveProgress.AddDynamic(
		this, &UCSTutorialObjectiveWidgetBase::HandleObjectiveProgress);
	TutorialSubsystem->OnTutorialCompleted.AddDynamic(
		this, &UCSTutorialObjectiveWidgetBase::HandleTutorialCompleted);
}

void UCSTutorialObjectiveWidgetBase::NativeDestruct()
{
	if (TutorialSubsystem != nullptr)
	{
		TutorialSubsystem->OnTutorialStepChanged.RemoveDynamic(
			this, &UCSTutorialObjectiveWidgetBase::HandleStepChanged);
		TutorialSubsystem->OnTutorialObjectiveProgress.RemoveDynamic(
			this, &UCSTutorialObjectiveWidgetBase::HandleObjectiveProgress);
		TutorialSubsystem->OnTutorialCompleted.RemoveDynamic(
			this, &UCSTutorialObjectiveWidgetBase::HandleTutorialCompleted);
	}

	Super::NativeDestruct();
}

void UCSTutorialObjectiveWidgetBase::HandleStepChanged(const FCSTutorialStepRow& NewStep)
{
	if (NewStep.ObjectiveText.IsEmpty())
	{
		OnObjectiveHidden();
		return;
	}

	OnObjectiveChanged(NewStep.ObjectiveText, NewStep.ObjectiveGoal > 0);
}

void UCSTutorialObjectiveWidgetBase::HandleObjectiveProgress(int32 Current, int32 Goal)
{
	OnObjectiveProgressChanged(Current, Goal);
}

void UCSTutorialObjectiveWidgetBase::HandleTutorialCompleted()
{
	OnObjectiveHidden();
}
