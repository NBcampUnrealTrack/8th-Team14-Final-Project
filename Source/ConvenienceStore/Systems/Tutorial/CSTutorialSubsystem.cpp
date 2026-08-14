#include "Systems/Tutorial/CSTutorialSubsystem.h"

#include "Engine/DataTable.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

// 1이면 게임 시작 시 튜토리얼을 건너뜀 (디버깅용)
static int32 GSkipTutorial = 0;
static FAutoConsoleVariableRef CVarSkipTutorial(
	TEXT("cs.SkipTutorial"),
	GSkipTutorial,
	TEXT("1이면 게임 시작 시 튜토리얼을 건너뜀. 디버깅용."),
	ECVF_Cheat);

// 콘솔에서 cs.TNext를 입력하면 현재 스텝을 강제로 다음으로 넘김
static void DebugAdvanceTutorialStepCommand(const TArray<FString>& Args, UWorld* World)
{
	if (UCSTutorialSubsystem* TutorialSubsystem =
		World != nullptr ? World->GetSubsystem<UCSTutorialSubsystem>() : nullptr)
	{
		TutorialSubsystem->DebugForceAdvanceStep();
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdAdvanceTutorialStep(
	TEXT("cs.TNext"),
	TEXT("현재 튜토리얼 스텝을 완료 조건과 무관하게 강제로 다음으로 넘긴다. 디버그용."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DebugAdvanceTutorialStepCommand));

void UCSTutorialSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const ACSStoreGameMode* GameMode = InWorld.GetAuthGameMode<ACSStoreGameMode>();
	if (!IsValid(GameMode) || !GameMode->IsTutorialEnabled())
	{
		// bTutorialEnabled가 false인 맵은 여기서 끝
		return;
	}

	const ACSStoreGameState* GameState = InWorld.GetGameState<ACSStoreGameState>();
	if (IsValid(GameState) && GameState->CurrentBusinessDay > 1)
	{
		// 2일차부터는 튜토리얼을 띄우지 않음
		return;
	}

	if (GSkipTutorial != 0)
	{
		CompleteTutorial();
		return;
	}

	StartTutorial(GameMode->GetTutorialStepDataTable());
}

void UCSTutorialSubsystem::Deinitialize()
{
	// 외부 델리게이트(진열대, UI 등)를 구독하게 되면 여기서 전부 RemoveDynamic 해야 함
	Super::Deinitialize();
}

void UCSTutorialSubsystem::StartTutorial(UDataTable* InStepDataTable)
{
	StepRows.Reset();

	if (!IsValid(InStepDataTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Tutorial] TutorialStepDataTable이 지정되지 않았습니다. BP_TutorialGameMode를 확인하세요."));
		return;
	}

	// DataTable에 행이 나열된 순서를 그대로 진행 순서로 사용
	for (const FName& RowName : InStepDataTable->GetRowNames())
	{
		if (FCSTutorialStepRow* Row = InStepDataTable->FindRow<FCSTutorialStepRow>(
			RowName, TEXT("UCSTutorialSubsystem::StartTutorial")))
		{
			StepRows.Add(Row);
		}
	}

	if (StepRows.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] TutorialStepDataTable에 행이 없습니다."));
		return;
	}

	CurrentStepIndex = 0;
	BroadcastCurrentStep();
}

void UCSTutorialSubsystem::AdvanceStep()
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const ECSTutorialPart FinishedPart = StepRows[CurrentStepIndex]->Part;
	const int32 NextIndex = CurrentStepIndex + 1;

	if (!StepRows.IsValidIndex(NextIndex))
	{
		HandlePartFinished(FinishedPart);
		return;
	}

	CurrentStepIndex = NextIndex;
	BroadcastCurrentStep();
}

void UCSTutorialSubsystem::HandlePartFinished(ECSTutorialPart FinishedPart)
{
	switch (FinishedPart)
	{
	case ECSTutorialPart::PreOpen:
		HandlePart1Finished();
		break;
	case ECSTutorialPart::DuringOpen:
		// 3부(영업 후) 스텝이 DataTable에 추가되면 여기서 PostClose로 이어감
		CompleteTutorial();
		break;
	case ECSTutorialPart::PostClose:
	default:
		CompleteTutorial();
		break;
	}
}

void UCSTutorialSubsystem::HandlePart1Finished()
{
	// 새 시간 흐름에서는 입간판을 여는 것 자체가 영업 시작 신호이므로,
	// 여기서 따로 시계를 재개시키는 코드가 필요 없음
	// 2부(영업 중) 스텝이 DataTable에 추가되면 여기서 CompleteTutorial() 대신 2부 첫 스텝으로 넘어가야 함
	CompleteTutorial();
}

void UCSTutorialSubsystem::CompleteTutorial()
{
	if (bTutorialCompleted)
	{
		return;
	}

	bTutorialCompleted = true;
	OnTutorialCompleted.Broadcast();

	// 위젯과 마커를 숨기는 코드를 여기 추가
}

void UCSTutorialSubsystem::BroadcastCurrentStep() const
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const FCSTutorialStepRow& CurrentRow = *StepRows[CurrentStepIndex];
	OnTutorialStepChanged.Broadcast(CurrentRow);

	if (CurrentRow.ObjectiveGoal > 0)
	{
		OnTutorialObjectiveProgress.Broadcast(0, CurrentRow.ObjectiveGoal);
	}
}

void UCSTutorialSubsystem::NotifyStepCompleted(ECSTutorialStepId CompletedStepId)
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	// 이미 지나간 스텝에서 뒤늦게 신호가 오면 무시
	if (StepRows[CurrentStepIndex]->StepId != CompletedStepId)
	{
		return;
	}

	AdvanceStep();
}

void UCSTutorialSubsystem::NotifyObjectiveProgress(ECSTutorialStepId StepId, int32 NewCurrent)
{
	if (!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != StepId)
	{
		return;
	}

	OnTutorialObjectiveProgress.Broadcast(NewCurrent, StepRows[CurrentStepIndex]->ObjectiveGoal);
}

void UCSTutorialSubsystem::DebugForceAdvanceStep()
{
	AdvanceStep();
}
