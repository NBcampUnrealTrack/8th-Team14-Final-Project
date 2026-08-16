#include "Systems/Tutorial/CSTutorialSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "Features/Trash/CSTrashActor.h"
#include "Features/Trash/CSTrashCanActor.h"
#include "Features/Tutorial/CSTutorialMarkerComponent.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Tutorial/CSTutorialBannerWidgetBase.h"
#include "UI/Tutorial/CSTutorialDialogueWidgetBase.h"
#include "UI/Tutorial/CSTutorialObjectiveWidgetBase.h"

// 발주/장부 UI 등 다른 화면 위에서도 튜토리얼 위젯이 항상 보이도록 충분히 높은 값을 사용
static constexpr int32 TutorialWidgetZOrder = 1000;

// TrySubscribeToPlayerController가 재시도를 포기하기까지 시도할 최대 횟수
static constexpr int32 MaxPlayerControllerSubscribeAttempts = 20;

// 재시도 사이에 둘 간격(초) / 체감상 딜레이가 없을 만큼 짧게 잡음
static constexpr float PlayerControllerSubscribeRetryInterval = 0.05f;

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
	ClearAutoAdvanceTimer();
	HideAllActiveMarkers();
	UnsubscribeFromLevelDisposables();
	UnsubscribeFromPlayerController();
	CloseTutorialWidgets();

	Super::Deinitialize();
}

void UCSTutorialSubsystem::StartTutorial(ACSStoreGameMode* GameMode)
{
	StepRows.Reset();
	CleanTrashDisposedCount = 0;

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
	SubscribeToLevelDisposables();
	TrySubscribeToPlayerController();

	CurrentStepIndex = 0;
	BroadcastCurrentStep();
	StartAutoAdvanceTimerIfNeeded();
	UpdateHighlightMarkersForCurrentStep();
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
	StartAutoAdvanceTimerIfNeeded();
	UpdateHighlightMarkersForCurrentStep();
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
	// 새 시간 흐름에서는 입간판을 여는 것 자체가 영업 시작 신호이므로
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
	ClearAutoAdvanceTimer();
	HideAllActiveMarkers();
	OnTutorialCompleted.Broadcast();

	UnsubscribeFromLevelDisposables();
	UnsubscribeFromPlayerController();
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

void UCSTutorialSubsystem::StartAutoAdvanceTimerIfNeeded()
{
	// 스텝이 바뀔 때마다 호출되므로 이전 스텝용으로 걸려 있던 타이머부터 확실히 정리
	ClearAutoAdvanceTimer();

	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const FCSTutorialStepRow& CurrentRow = *StepRows[CurrentStepIndex];

	// 관문형(bBlocking == true)은 완료 신호를 기다려야 하므로 타이머를 걸지 않음
	// AutoAdvanceSeconds가 0 이하인 경우도 마찬가지로 걸지 않음
	if (CurrentRow.bBlocking || CurrentRow.AutoAdvanceSeconds <= 0.0f)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// AutoAdvanceSeconds초 후 AdvanceStep()을 한 번만(bLoop=false) 자동 호출
		World->GetTimerManager().SetTimer(
			AutoAdvanceTimerHandle,
			this,
			&UCSTutorialSubsystem::AdvanceStep,
			CurrentRow.AutoAdvanceSeconds,
			false);
	}
}

void UCSTutorialSubsystem::ClearAutoAdvanceTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoAdvanceTimerHandle);
	}
}

void UCSTutorialSubsystem::UpdateHighlightMarkersForCurrentStep()
{
	// 스텝이 바뀔 때마다 호출되므로 이전 스텝에서 켜둔 마커부터 전부 끔
	HideAllActiveMarkers();

	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const FCSTutorialStepRow& CurrentRow = *StepRows[CurrentStepIndex];

	// 태그가 비어 있으면 이 스텝은 마커를 쓰지 않음
	if (CurrentRow.HighlightMarkerTag.IsNone())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 같은 태그를 가진 액터가 여러 개면 전부 켬
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || !Actor->ActorHasTag(CurrentRow.HighlightMarkerTag))
		{
			continue;
		}

		if (UCSTutorialMarkerComponent* MarkerComponent = Actor->FindComponentByClass<UCSTutorialMarkerComponent>())
		{
			MarkerComponent->ShowMarker(CurrentRow.ObjectiveText);
			ActiveMarkerComponents.Add(MarkerComponent);
		}
	}
}

void UCSTutorialSubsystem::HideAllActiveMarkers()
{
	for (UCSTutorialMarkerComponent* MarkerComponent : ActiveMarkerComponents)
	{
		if (IsValid(MarkerComponent))
		{
			MarkerComponent->HideMarker();
		}
	}

	ActiveMarkerComponents.Reset();
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

	const int32 Goal = StepRows[CurrentStepIndex]->ObjectiveGoal;
	OnTutorialObjectiveProgress.Broadcast(NewCurrent, Goal);

	// 목표 수치에 도달하면 자동으로 다음 스텝으로 넘어감
	if (Goal > 0 && NewCurrent >= Goal)
	{
		AdvanceStep();
	}
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

void UCSTutorialSubsystem::SubscribeToLevelDisposables()
{
	if (GetWorld() == nullptr)
	{
		return;
	}

	// 레벨에 배치된 쓰레기통을 전부 찾아 구독
	// 쓰레기통은 튜토리얼의 존재를 모르므로 서브시스템이 먼저 찾아가서 연결
	for (TActorIterator<ACSTrashCanActor> It(GetWorld()); It; ++It)
	{
		ACSTrashCanActor* TrashCan = *It;
		if (!IsValid(TrashCan))
		{
			continue;
		}

		TrashCan->OnItemDisposed.AddDynamic(this, &UCSTutorialSubsystem::HandleTrashDisposed);
		SubscribedTrashCans.Add(TrashCan);
	}
}

void UCSTutorialSubsystem::UnsubscribeFromLevelDisposables()
{
	for (ACSTrashCanActor* TrashCan : SubscribedTrashCans)
	{
		if (IsValid(TrashCan))
		{
			TrashCan->OnItemDisposed.RemoveDynamic(this, &UCSTutorialSubsystem::HandleTrashDisposed);
		}
	}

	SubscribedTrashCans.Reset();
}

void UCSTutorialSubsystem::TrySubscribeToPlayerController()
{
	// 그 사이 튜토리얼이 이미 끝났다면 재시도를 멈춤 (cs.TNext 사용해서 조기 종료된 경우)
	if (bTutorialCompleted)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	ACSStorePlayerController* PlayerController = Cast<ACSStorePlayerController>(World->GetFirstPlayerController());
	if (IsValid(PlayerController))
	{
		PlayerControllerSubscribeAttempts = 0;
		SubscribeToPlayerController(PlayerController);
		return;
	}

	// 아직 컨트롤러가 스폰되지 않음 -> 짧은 텀 후 다시 시도
	// 정상적인 경우 한두 번 안에 찾아지지만 혹시 모를 상황을 대비해 재시도 횟수를 제한
	++PlayerControllerSubscribeAttempts;
	if (PlayerControllerSubscribeAttempts > MaxPlayerControllerSubscribeAttempts)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Tutorial] PlayerController를 %d회 시도해도 찾지 못해 구독을 포기합니다."),
			MaxPlayerControllerSubscribeAttempts);
		return;
	}

	World->GetTimerManager().SetTimer(
		PlayerControllerSubscribeRetryTimerHandle,
		this,
		&UCSTutorialSubsystem::TrySubscribeToPlayerController,
		PlayerControllerSubscribeRetryInterval,
		false);
}

void UCSTutorialSubsystem::SubscribeToPlayerController(ACSStorePlayerController* PlayerController)
{
	SubscribedPlayerController = PlayerController;

	PlayerController->OnGameplayUIInputLockChanged.AddDynamic(
		this, &UCSTutorialSubsystem::HandleGameplayUIInputLockChanged);
}

void UCSTutorialSubsystem::UnsubscribeFromPlayerController()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerControllerSubscribeRetryTimerHandle);
	}

	if (IsValid(SubscribedPlayerController))
	{
		SubscribedPlayerController->OnGameplayUIInputLockChanged.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleGameplayUIInputLockChanged);
	}

	SubscribedPlayerController = nullptr;
}

void UCSTutorialSubsystem::HandleGameplayUIInputLockChanged(UUserWidget* RequestingWidget, bool bLocked)
{
	if (!StepRows.IsValidIndex(CurrentStepIndex) || !IsValid(RequestingWidget))
	{
		return;
	}

	const FCSTutorialStepRow& CurrentRow = *StepRows[CurrentStepIndex];

	// 이 스텝이 UI 감지를 쓰지 않으면(빈 칸이면) 신경 쓰지 않음
	if (CurrentRow.ExpectedWidgetClass.IsNull())
	{
		return;
	}

	// 필요한 시점에만 실제로 로드 (평소엔 주소만 갖고 있다가 지금 처음 불러옴)
	const UClass* ExpectedClass = CurrentRow.ExpectedWidgetClass.LoadSynchronous();
	if (ExpectedClass == nullptr || !RequestingWidget->IsA(ExpectedClass))
	{
		// 지금 열리거나 닫힌 위젯이 이 스텝이 기다리는 위젯이 아니면 무시
		return;
	}

	// bCompleteOnWidgetClosed == false면 "열릴 때"(bLocked == true) 완료
	// bCompleteOnWidgetClosed == true면 "닫힐 때"(bLocked == false) 완료
	const bool bWaitingForOpen = !CurrentRow.bCompleteOnWidgetClosed;
	const bool bWidgetJustOpened = bLocked;
	const bool bWidgetJustClosed = !bLocked;

	const bool bCompletesNow =
		(bWaitingForOpen && bWidgetJustOpened) || (!bWaitingForOpen && bWidgetJustClosed);

	if (bCompletesNow)
	{
		NotifyStepCompleted(CurrentRow.StepId);
	}
}

void UCSTutorialSubsystem::HandleTrashDisposed(AActor* DisposedItem, int32 TotalDisposedCount)
{
	(void)TotalDisposedCount;

	if (Cast<ACSTrashActor>(DisposedItem) == nullptr ||
		!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::CleanTrash)
	{
		return;
	}

	if (CleanTrashDisposedCount < MAX_int32)
	{
		++CleanTrashDisposedCount;
	}

	NotifyObjectiveProgress(
		ECSTutorialStepId::CleanTrash,
		CleanTrashDisposedCount);
}
