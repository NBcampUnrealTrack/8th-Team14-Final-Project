#include "Systems/Tutorial/CSTutorialSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/Tutorial/CSTutorialBannerWidgetBase.h"
#include "UI/Tutorial/CSTutorialDialogueWidgetBase.h"
#include "UI/Tutorial/CSTutorialObjectiveWidgetBase.h"

// 발주/장부 UI 등 다른 화면 위에서도 튜토리얼 위젯이 항상 보이도록 충분히 높은 값을 사용
static constexpr int32 TutorialWidgetZOrder = 1000;

// 1이면 게임 시작 시 튜토리얼을 건너뜀 (디버깅용)
static int32 GSkipTutorial = 0;
static FAutoConsoleVariableRef CVarSkipTutorial(
	TEXT("cs.SkipTutorial"),
	GSkipTutorial,
	TEXT("1이면 게임 시작 시 튜토리얼을 건너뛴다. 디버깅용."),
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

	ACSStoreGameMode* GameMode = InWorld.GetAuthGameMode<ACSStoreGameMode>();
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

	StartTutorial(GameMode);
}

void UCSTutorialSubsystem::Deinitialize()
{
	// 외부 델리게이트(진열대, UI 등)를 구독하게 되면 여기서 전부 RemoveDynamic 해야 함
	// 화면에 떠 있던 위젯은 안전하게 여기서도 한 번 더 정리
	CloseTutorialWidgets();

	Super::Deinitialize();
}

void UCSTutorialSubsystem::StartTutorial(ACSStoreGameMode* GameMode)
{
	StepRows.Reset();

	UDataTable* StepDataTable = IsValid(GameMode) ? GameMode->GetTutorialStepDataTable() : nullptr;
	if (!IsValid(StepDataTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Tutorial] TutorialStepDataTable이 지정되지 않았습니다. BP_TutorialGameMode를 확인하세요."));
		return;
	}

	// DataTable에 행이 나열된 순서를 그대로 진행 순서로 사용
	for (const FName& RowName : StepDataTable->GetRowNames())
	{
		if (FCSTutorialStepRow* Row = StepDataTable->FindRow<FCSTutorialStepRow>(
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

	CreateTutorialWidgets(GameMode);

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

	CloseTutorialWidgets();
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

void UCSTutorialSubsystem::CreateTutorialWidgets(ACSStoreGameMode* GameMode)
{
	if (!IsValid(GameMode) || GetWorld() == nullptr)
	{
		return;
	}

	if (TSubclassOf<UCSTutorialBannerWidgetBase> BannerClass = GameMode->GetTutorialBannerWidgetClass())
	{
		BannerWidgetInstance = CreateWidget<UCSTutorialBannerWidgetBase>(GetWorld(), BannerClass);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] TutorialBannerWidgetClass가 지정되지 않았습니다."));
	}

	if (TSubclassOf<UCSTutorialDialogueWidgetBase> DialogueClass = GameMode->GetTutorialDialogueWidgetClass())
	{
		DialogueWidgetInstance = CreateWidget<UCSTutorialDialogueWidgetBase>(GetWorld(), DialogueClass);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] TutorialDialogueWidgetClass가 지정되지 않았습니다."));
	}

	if (TSubclassOf<UCSTutorialObjectiveWidgetBase> ObjectiveClass = GameMode->GetTutorialObjectiveWidgetClass())
	{
		ObjectiveWidgetInstance = CreateWidget<UCSTutorialObjectiveWidgetBase>(GetWorld(), ObjectiveClass);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] TutorialObjectiveWidgetClass가 지정되지 않았습니다."));
	}

	if (IsValid(BannerWidgetInstance))
	{
		BannerWidgetInstance->SetVisibility(ESlateVisibility::HitTestInvisible);
		BannerWidgetInstance->AddToViewport(TutorialWidgetZOrder);
	}

	if (IsValid(DialogueWidgetInstance))
	{
		DialogueWidgetInstance->SetVisibility(ESlateVisibility::HitTestInvisible);
		DialogueWidgetInstance->AddToViewport(TutorialWidgetZOrder);
	}

	if (IsValid(ObjectiveWidgetInstance))
	{
		ObjectiveWidgetInstance->SetVisibility(ESlateVisibility::HitTestInvisible);
		ObjectiveWidgetInstance->AddToViewport(TutorialWidgetZOrder);
	}
}

void UCSTutorialSubsystem::CloseTutorialWidgets()
{
	if (IsValid(BannerWidgetInstance))
	{
		BannerWidgetInstance->RemoveFromParent();
		BannerWidgetInstance = nullptr;
	}

	if (IsValid(DialogueWidgetInstance))
	{
		DialogueWidgetInstance->RemoveFromParent();
		DialogueWidgetInstance = nullptr;
	}

	if (IsValid(ObjectiveWidgetInstance))
	{
		ObjectiveWidgetInstance->RemoveFromParent();
		ObjectiveWidgetInstance = nullptr;
	}
}
