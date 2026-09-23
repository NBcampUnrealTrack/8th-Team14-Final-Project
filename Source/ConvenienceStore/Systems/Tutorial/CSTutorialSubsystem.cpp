#include "Systems/Tutorial/CSTutorialSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Tutorial/CSTutorialShelfSpawnPoint.h"
#include "Features/Tutorial/CSTutorialCrateSpawnPoint.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Trash/CSTrashActor.h"
#include "Features/Trash/CSTrashCanActor.h"
#include "Features/Tutorial/CSTutorialMarkerComponent.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/Ordering/CSOrderSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "UI/Store/CSDailySummaryWidget.h"
#include "UI/Tutorial/CSTutorialBannerWidgetBase.h"
#include "UI/Tutorial/CSTutorialDialogueWidgetBase.h"
#include "UI/Tutorial/CSTutorialObjectiveWidgetBase.h"

// 발주창(WBP_Orderv2, 1100번 레이어) 등 다른 화면 위에서도 항상 보이도록 그보다 높게 잡음
static constexpr int32 TutorialWidgetZOrder = 1150;

// TrySubscribeToPlayerController가 재시도를 포기하기까지 시도할 최대 횟수
static constexpr int32 MaxPlayerControllerSubscribeAttempts = 20;

// 재시도 사이에 둘 간격(초) / 체감상 딜레이가 없을 만큼 짧게 잡음
static constexpr float PlayerControllerSubscribeRetryInterval = 0.05f;

// TryStartTutorialAfterTitleScreen이 재시도를 포기하기까지 시도할 최대 횟수
// 임시 타이틀은 사람이 직접 눌러야 닫히므로 플레이어 컨트롤러 재시도보다 훨씬 길게 기다려줘야 함
static constexpr int32 MaxTitleScreenWaitAttempts = 1200;

// 재시도 사이 간격(초). 0.25초 * 1200회 = 최대 5분까지 대기
static constexpr float TitleScreenWaitRetryInterval = 0.25f;

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

// 콘솔에서 cs.TSkip을 입력하면 배너/대화창/목표/마커(외곽선)까지 튜토리얼을 통째로 끔
static void DebugSkipTutorialCommand(const TArray<FString>& Args, UWorld* World)
{
	if (UCSTutorialSubsystem* TutorialSubsystem =
		World != nullptr ? World->GetSubsystem<UCSTutorialSubsystem>() : nullptr)
	{
		TutorialSubsystem->DebugSkipTutorial();
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdSkipTutorial(
	TEXT("cs.TSkip"),
	TEXT("튜토리얼을 배너·대화창·목표·마커(외곽선)까지 전부 끄고 완전히 종료한다. 디버그용."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DebugSkipTutorialCommand));

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

	// OnWorldBeginPlay가 PlayerController::BeginPlay(SetPause(true) 호출 지점)보다 먼저 실행돼
	// 바로 확인하면 아직 안 멈춘 것으로 잘못 판단함 - 타이머로 한 틱 이상 미뤄서 피함
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			TitleScreenWaitTimerHandle,
			this,
			&UCSTutorialSubsystem::TryStartTutorialAfterTitleScreen,
			TitleScreenWaitRetryInterval,
			false);
	}
}

void UCSTutorialSubsystem::TryStartTutorialAfterTitleScreen()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 임시 타이틀 화면이 SetPause(true)로 게임을 멈춰둔 동안에는 튜토리얼을 시작하지 않음
	// "시작하기"를 눌러 실제로 게임이 재개될 때(IsPaused() == false)까지 대기
	const APlayerController* PlayerController = World->GetFirstPlayerController();
	const bool bReadyToStart = PlayerController != nullptr && !PlayerController->IsPaused();

	if (!bReadyToStart)
	{
		++TitleScreenWaitAttempts;
		if (TitleScreenWaitAttempts <= MaxTitleScreenWaitAttempts)
		{
			World->GetTimerManager().SetTimer(
				TitleScreenWaitTimerHandle,
				this,
				&UCSTutorialSubsystem::TryStartTutorialAfterTitleScreen,
				TitleScreenWaitRetryInterval,
				false);
			return;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[Tutorial] 임시 타이틀 화면 종료를 %d회 시도해도 감지하지 못해 튜토리얼을 그냥 시작합니다."),
			MaxTitleScreenWaitAttempts);
	}

	ACSStoreGameMode* GameMode = World->GetAuthGameMode<ACSStoreGameMode>();
	if (!IsValid(GameMode) || !GameMode->IsTutorialEnabled())
	{
		return;
	}

	StartTutorial(GameMode);
}

void UCSTutorialSubsystem::Deinitialize()
{
	ClearAutoAdvanceTimer();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TitleScreenWaitTimerHandle);
	}
	HideAllActiveMarkers();
	UnsubscribeFromLevelDisposables();
	UnsubscribeFromLevelShelves();
	UnsubscribeFromPlayerCharacter();
	UnsubscribeFromPlayerController();
	UnsubscribeFromOrderSubsystem();
	UnsubscribeFromStoreGameState();
	UnsubscribeFromCustomerRuntimeSubsystem();
	CloseTutorialWidgets();

	Super::Deinitialize();
}

void UCSTutorialSubsystem::StartTutorial(ACSStoreGameMode* GameMode)
{
	StepRows.Reset();
	TipRows.Reset();
	ShownTipIds.Reset();
	bTipChainActive = false;
	bPart3EntryScheduled = false;
	bOrderWidgetCurrentlyOpen = false;
	bOrderWidgetOrderedThisOpen = false;
	bOrderWidgetFacilityTabViewedThisOpen = false;
	CleanTrashDisposedCount = 0;
	StockShelvesPlacedCount = 0;
	bTutorialShelfSpawned = false;
	bTutorialCratesSpawned = false;

	UDataTable* StepDataTable = IsValid(GameMode) ? GameMode->GetTutorialStepDataTable() : nullptr;
	if (!IsValid(StepDataTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Tutorial] TutorialStepDataTable이 지정되지 않았습니다. BP_TutorialGameMode를 확인하세요."));
		return;
	}

	// DataTable에 행이 나열된 순서를 그대로 진행 순서로 사용
	// DuringOpen(2부 원포인트 팁) 행은 관문형 진행(StepRows)이 아니라 TipRows로 따로 담음
	for (const FName& RowName : StepDataTable->GetRowNames())
	{
		if (FCSTutorialStepRow* Row = StepDataTable->FindRow<FCSTutorialStepRow>(
			RowName, TEXT("UCSTutorialSubsystem::StartTutorial")))
		{
			if (Row->Part == ECSTutorialPart::DuringOpen)
			{
				TipRows.Add(Row);
			}
			else
			{
				StepRows.Add(Row);
			}
		}
	}

	if (StepRows.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] TutorialStepDataTable에 행이 없습니다."));
		return;
	}

	CreateTutorialWidgets(GameMode);
	SubscribeToLevelDisposables();
	SubscribeToLevelShelves();
	TrySubscribeToPlayerController();
	SubscribeToOrderSubsystem();
	SubscribeToStoreGameState();
	SubscribeToCustomerRuntimeSubsystem();

	CurrentStepIndex = 0;
	BroadcastCurrentStep();
	StartAutoAdvanceTimerIfNeeded();
	UpdateHighlightMarkersForCurrentStep();
	TrySpawnTutorialShelfDelivery();
	TrySpawnTutorialCrateDeliveries();
}

void UCSTutorialSubsystem::AdvanceStep()
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const ECSTutorialPart FinishedPart = StepRows[CurrentStepIndex]->Part;
	const int32 NextIndex = CurrentStepIndex + 1;

	// 다음 행이 없거나, 있어도 소속 Part가 지금과 다르면 지금 Part는 여기서 끝난 것으로 봄
	// (다음 Part 진입은 HandlePartFinished가 아니라 각 Part의 실제 트리거가 맡음)
	// 이 체크가 없으면 3부 스텝이 1부 바로 다음 행으로 DT에 있을 때 영업 시작과 동시에 떠버림
	if (!StepRows.IsValidIndex(NextIndex) || StepRows[NextIndex]->Part != FinishedPart)
	{
		HandlePartFinished(FinishedPart);
		return;
	}

	CurrentStepIndex = NextIndex;
	BroadcastCurrentStep();
	StartAutoAdvanceTimerIfNeeded();
	UpdateHighlightMarkersForCurrentStep();
	TrySpawnTutorialShelfDelivery();
	TrySpawnTutorialCrateDeliveries();
}

void UCSTutorialSubsystem::HandlePartFinished(ECSTutorialPart FinishedPart)
{
	switch (FinishedPart)
	{
	case ECSTutorialPart::PreOpen:
		HandlePart1Finished();
		break;
	case ECSTutorialPart::DuringOpen:
		// 2부는 관문형 스텝을 두지 않기로 했으므로(StepRows에 안 들어감) 실제로는 여기 안 옴
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
	// OpenSign의 마커·대화창·목표 표시가 2부 내내 화면에 남아있던 문제 - 다음 스텝이 없어
	// BroadcastCurrentStep이 다시 안 불리므로 여기서 명시적으로 정리해야 함
	ClearTutorialDisplay();

	// 3부(마감 후) 스텝이 없으면 예전처럼 여기서 바로 종료. 있으면 구독을 유지한 채 대기하고,
	// 실제 3부 시작은 HandleStorePhaseChanged(ClosingFreeRoam)가 맡음
	if (FindFirstStepIndexForPart(ECSTutorialPart::PostClose) == INDEX_NONE)
	{
		CompleteTutorial();
		return;
	}

	// 다이어리 팁은 고정 시간이 아니라 "영업을 시작하였습니다" 안내가 끝나는 시점(HandleStoreOpenedAnnouncementFinished)에 뜸
	bTipChainActive = true;
}

void UCSTutorialSubsystem::CompleteTutorial()
{
	if (bTutorialCompleted)
	{
		return;
	}

	bTutorialCompleted = true;
	bTipChainActive = false;
	ClearAutoAdvanceTimer();
	HideAllActiveMarkers();
	OnTutorialCompleted.Broadcast();

	UnsubscribeFromLevelDisposables();
	UnsubscribeFromLevelShelves();
	UnsubscribeFromPlayerCharacter();
	UnsubscribeFromPlayerController();
	UnsubscribeFromOrderSubsystem();
	UnsubscribeFromStoreGameState();
	UnsubscribeFromCustomerRuntimeSubsystem();
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

	// ObjectiveGoal이 0이어도 방송해야 함
	OnTutorialObjectiveProgress.Broadcast(0, CurrentRow.ObjectiveGoal);

	// 정산창(C) 등으로 화면이 잠깐 가려졌다가 그대로 되살아날 때 쓸 최신 상태를 기억해둠
	LastBroadcastRow = &CurrentRow;
	LastBroadcastObjectiveCurrent = 0;
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
	ShowMarkersForTag(CurrentRow.HighlightMarkerTag, CurrentRow.MarkerLabelText);
}

void UCSTutorialSubsystem::ShowMarkersForTag(FName Tag, const FText& Label)
{
	// 태그가 비어 있으면 이 스텝/팁은 마커를 쓰지 않음
	if (Tag.IsNone())
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
		if (!IsValid(Actor) || !Actor->ActorHasTag(Tag))
		{
			continue;
		}

		if (UCSTutorialMarkerComponent* MarkerComponent = Actor->FindComponentByClass<UCSTutorialMarkerComponent>())
		{
			MarkerComponent->ShowMarker(Label);
			ActiveMarkerComponents.Add(MarkerComponent);
		}
	}
}

void UCSTutorialSubsystem::TrySpawnTutorialShelfDelivery()
{
	if (bTutorialShelfSpawned ||
		!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::DeliveryArrived)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	ACSStoreGameMode* GameMode = World->GetAuthGameMode<ACSStoreGameMode>();
	const TSubclassOf<AActor> DefaultShelfClass =
		IsValid(GameMode) ? GameMode->GetTutorialShelfFacilityClass() : nullptr;

	// 레벨에 배치된 스폰 지점마다 선반을 하나씩 스폰 (배송 상자와 동일한 패턴)
	for (TActorIterator<ACSTutorialShelfSpawnPoint> It(World); It; ++It)
	{
		ACSTutorialShelfSpawnPoint* SpawnPoint = *It;
		if (!IsValid(SpawnPoint))
		{
			continue;
		}

		// 스폰 지점에 클래스가 지정돼 있으면 그걸 우선 사용, 없으면 GameMode 기본값
		const TSubclassOf<AActor> ShelfClass =
			SpawnPoint->ShelfClassOverride != nullptr ? SpawnPoint->ShelfClassOverride : DefaultShelfClass;

		if (ShelfClass == nullptr)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Tutorial] 스폰 지점(%s)에 쓸 선반 클래스가 없습니다 (ShelfClassOverride도, GameMode 기본값도 없음)."),
				*SpawnPoint->GetName());
			continue;
		}

		AActor* SpawnedShelf = World->SpawnActor<AActor>(ShelfClass, SpawnPoint->GetActorTransform());

		if (!IsValid(SpawnedShelf))
		{
			continue;
		}

		TutorialSpawnedShelves.Add(SpawnedShelf);

		if (SpawnedShelf->GetClass()->ImplementsInterface(UCSBuildableFacility::StaticClass()))
		{
			ICSBuildableFacility::Execute_InitializeAsUninstalledFacility(SpawnedShelf);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Tutorial] TutorialShelfFacilityClass(%s)가 ICSBuildableFacility를 구현하지 않습니다."),
				*ShelfClass->GetName());
		}

		UCSTutorialMarkerComponent* HighlightComponent =
			NewObject<UCSTutorialMarkerComponent>(SpawnedShelf);
		if (IsValid(HighlightComponent))
		{
			HighlightComponent->SetupAttachment(SpawnedShelf->GetRootComponent());
			HighlightComponent->RegisterComponent();
			HighlightComponent->ShowMarker(FText::GetEmpty());
		}
	}

	bTutorialShelfSpawned = true;
}

void UCSTutorialSubsystem::TrySpawnTutorialCrateDeliveries()
{
	if (bTutorialCratesSpawned ||
		!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::StockShelves)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 레벨에 배치된 크레이트 스폰 지점마다 각자 지정된 상품/수량으로 상자를 하나씩 스폰
	for (TActorIterator<ACSTutorialCrateSpawnPoint> It(World); It; ++It)
	{
		ACSTutorialCrateSpawnPoint* SpawnPoint = *It;
		if (!IsValid(SpawnPoint) || SpawnPoint->CrateClass == nullptr)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Tutorial] ACSTutorialCrateSpawnPoint에 CrateClass가 지정되지 않았습니다."));
			continue;
		}

		AActor* SpawnedActor = World->SpawnActor<AActor>(
			SpawnPoint->CrateClass, SpawnPoint->GetActorTransform());
		ACSDeliveryCrateActor* SpawnedCrate = Cast<ACSDeliveryCrateActor>(SpawnedActor);
		if (!IsValid(SpawnedCrate))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Tutorial] CrateClass(%s)가 ACSDeliveryCrateActor 파생 클래스가 아닙니다."),
				*SpawnPoint->CrateClass->GetName());
			continue;
		}

		if (!SpawnedCrate->InitializeCrate(SpawnPoint->ProductRow, SpawnPoint->Quantity))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Tutorial] 튜토리얼 배송 상자 초기화에 실패했습니다 (스폰 지점: %s)."),
				*SpawnPoint->GetName());
		}

		TutorialSpawnedCrates.Add(SpawnedCrate);

		// 상자에도 동적으로 마커를 붙여 즉시 아웃라인을 켬
		UCSTutorialMarkerComponent* HighlightComponent =
			NewObject<UCSTutorialMarkerComponent>(SpawnedCrate);
		if (IsValid(HighlightComponent))
		{
			HighlightComponent->SetupAttachment(SpawnedCrate->GetRootComponent());
			HighlightComponent->RegisterComponent();
			HighlightComponent->ShowMarker(FText::GetEmpty());
		}
	}

	bTutorialCratesSpawned = true;
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

int32 UCSTutorialSubsystem::FindFirstStepIndexForPart(ECSTutorialPart TargetPart) const
{
	for (int32 Index = 0; Index < StepRows.Num(); ++Index)
	{
		if (StepRows[Index]->Part == TargetPart)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void UCSTutorialSubsystem::ShowOneShotTip(ECSTutorialStepId TipId)
{
	if (ShownTipIds.Contains(TipId))
	{
		return;
	}

	FCSTutorialStepRow** FoundRow = TipRows.FindByPredicate(
		[TipId](const FCSTutorialStepRow* Row) { return Row->StepId == TipId; });
	if (FoundRow == nullptr)
	{
		return;
	}

	ShownTipIds.Add(TipId);

	// 기존 배너/대화창/목표 위젯 구독을 그대로 재사용. 여기서는 CurrentStepIndex를 건드리지 않고
	// 방송만 함. 이전에 켜져 있던 마커는 여기서 새로 켜기 전에 정리
	HideAllActiveMarkers();
	const FCSTutorialStepRow& TipRow = **FoundRow;
	OnTutorialStepChanged.Broadcast(TipRow);
	OnTutorialObjectiveProgress.Broadcast(0, TipRow.ObjectiveGoal);
	ShowMarkersForTag(TipRow.HighlightMarkerTag, TipRow.MarkerLabelText);

	// LastBroadcastRow(정산창 닫을 때 되살릴 화면)는 여기서 갱신하지 않음. 팁은 BP 쪽 자체 타이머로
	// 알아서 사라지는 1회성 알림이라, 이미 사라진 지 오래인 팁이 정산창을 닫을 때마다 되살아나는
	// 문제가 있었음(예: 속마음 팁이 계속 재등장). 진행 중인 스텝(BroadcastCurrentStep)만 되살림 대상으로 둠
}

void UCSTutorialSubsystem::HandleStoreOpenedAnnouncementFinished()
{
	// 1부가 아직 안 끝났거나(팁 체인 대상 아님) 이미 3부로 넘어갔으면 무관한 시점이므로 무시
	if (!bTipChainActive)
	{
		return;
	}

	ShowOneShotTip(ECSTutorialStepId::TipDiary);
	// TipNoticeBoard는 다이어리를 닫는 시점(HandleTipWidgetLockChanged)에 트리거됨
	// TipSettlement는 게시판을 닫는 시점(HandleTipWidgetLockChanged)에 트리거됨
	// TipInnerThought는 첫 속마음 표시 이벤트(HandleInnerThoughtShown)로 트리거됨
}

bool UCSTutorialSubsystem::DoesTipExpectWidget(ECSTutorialStepId TipId, const UUserWidget* Widget) const
{
	if (!IsValid(Widget))
	{
		return false;
	}

	FCSTutorialStepRow* const* FoundRow = TipRows.FindByPredicate(
		[TipId](const FCSTutorialStepRow* Row) { return Row->StepId == TipId; });
	if (FoundRow == nullptr || (*FoundRow)->ExpectedWidgetClass.IsNull())
	{
		return false;
	}

	// 필요한 시점에만 실제로 로드 (HandleGameplayUIInputLockChanged와 동일한 패턴)
	const UClass* ExpectedClass = (*FoundRow)->ExpectedWidgetClass.LoadSynchronous();
	return ExpectedClass != nullptr && Widget->IsA(ExpectedClass);
}

void UCSTutorialSubsystem::ClearTutorialDisplay()
{
	HideAllActiveMarkers();

	// PromptType=None으로 완전히 숨기면 대화창 WBP가 이후 텍스트 변경으로 다시 안 보여주는
	// 버그가 있어, 완전히 숨기지 않고 빈 텍스트로만 바꿈
	FCSTutorialStepRow BlankRow;
	BlankRow.PromptType = ECSTutorialPromptType::Dialogue;
	BlankRow.PromptText = FText::GetEmpty();
	OnTutorialStepChanged.Broadcast(BlankRow);
	OnTutorialObjectiveProgress.Broadcast(0, 0);

	// 여기서 지워진 화면은 이미 끝났거나 다른 스텝으로 넘어가는 중이라, 정산창을 닫아도
	// 되살릴 이전 화면이 없는 것으로 취급함 (되살릴 내용이 있으면 그 직후 BroadcastCurrentStep/
	// ShowOneShotTip이 다시 채워줌)
	LastBroadcastRow = nullptr;
}

void UCSTutorialSubsystem::HandleTipWidgetLockChanged(UUserWidget* RequestingWidget, bool bLocked)
{
	// 1부가 아직 안 끝났거나(대기 중이 아님) 3부가 시작된 뒤면 팁 체인과 무관한 시점이므로 무시
	if (!bTipChainActive)
	{
		return;
	}

	if (DoesTipExpectWidget(ECSTutorialStepId::TipDiary, RequestingWidget))
	{
		if (bLocked)
		{
			// 팁이 아직 안 떴어도(안내 애니메이션이 끝나길 대기 중) 다이어리를 직접 열었으면
			// 나중에 안내가 끝나도 다시 뜨지 않도록 이미 본 것으로 표시함
			ShownTipIds.Add(ECSTutorialStepId::TipDiary);
			ClearTutorialDisplay();
		}
		else
		{
			// 다이어리를 닫는 순간 게시판 팁으로 넘어감 (이미 봤으면 ShowOneShotTip이 알아서 무시)
			ShowOneShotTip(ECSTutorialStepId::TipNoticeBoard);
		}
		return;
	}

	if (DoesTipExpectWidget(ECSTutorialStepId::TipNoticeBoard, RequestingWidget))
	{
		if (bLocked)
		{
			// 게시판과 상호작용(열림)하는 순간 바로 숨김
			ClearTutorialDisplay();
		}
		else
		{
			// 게시판을 닫는 순간 정산창 팁으로 넘어감 (이미 봤으면 ShowOneShotTip이 알아서 무시)
			ShowOneShotTip(ECSTutorialStepId::TipSettlement);
		}
		return;
	}
}

void UCSTutorialSubsystem::HandleDailySummaryWidgetLockChanged(UUserWidget* RequestingWidget, bool bLocked)
{
	// 실시간 정산창(C)은 스텝·팁 진행과 무관하게 아무 때나 열 수 있어 위 두 핸들러가 다루지 않음
	if (Cast<UCSDailySummaryWidget>(RequestingWidget) == nullptr)
	{
		return;
	}

	if (bLocked)
	{
		// 정산창이 튜토리얼 안내 위에 겹치지 않도록 지금 화면을 잠깐 숨김
		// (ClearTutorialDisplay는 되살릴 화면이 없는 것으로 취급해버리므로 여기서 직접 블랭크 방송)
		bDisplaySuspendedForDailySummary = true;
		HideAllActiveMarkers();
		FCSTutorialStepRow BlankRow;
		BlankRow.PromptType = ECSTutorialPromptType::Dialogue;
		BlankRow.PromptText = FText::GetEmpty();
		OnTutorialStepChanged.Broadcast(BlankRow);
		OnTutorialObjectiveProgress.Broadcast(0, 0);
		return;
	}

	if (!bDisplaySuspendedForDailySummary)
	{
		return;
	}
	bDisplaySuspendedForDailySummary = false;

	// 정산창을 닫으면 스텝을 넘기지 않고 숨기기 전 화면을 그대로 되살림
	if (LastBroadcastRow != nullptr)
	{
		OnTutorialStepChanged.Broadcast(*LastBroadcastRow);
		OnTutorialObjectiveProgress.Broadcast(LastBroadcastObjectiveCurrent, LastBroadcastRow->ObjectiveGoal);
		ShowMarkersForTag(LastBroadcastRow->HighlightMarkerTag, LastBroadcastRow->MarkerLabelText);
	}
}

void UCSTutorialSubsystem::NotifyStepCompleted(ECSTutorialStepId CompletedStepId)
{
	// 트리거 볼륨 등 구독/해제 목록과 무관하게 직접 호출하는 경로가 있어서, 튜토리얼이
	// 끝난 뒤에도 이 가드가 없으면 스텝이 되살아나 마커가 다시 켜짐(cs.TSkip 이후 재현됨)
	if (bTutorialCompleted || !StepRows.IsValidIndex(CurrentStepIndex))
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
	if (bTutorialCompleted ||
		!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != StepId)
	{
		return;
	}

	const int32 Goal = StepRows[CurrentStepIndex]->ObjectiveGoal;
	OnTutorialObjectiveProgress.Broadcast(NewCurrent, Goal);
	LastBroadcastObjectiveCurrent = NewCurrent;

	// 목표 수치에 도달하면 자동으로 다음 스텝으로 넘어감
	if (Goal > 0 && NewCurrent >= Goal)
	{
		AdvanceStep();
	}
}

void UCSTutorialSubsystem::DebugForceAdvanceStep()
{
	if (StepRows.IsValidIndex(CurrentStepIndex) &&
		StepRows[CurrentStepIndex]->StepId == ECSTutorialStepId::CleanTrash)
	{
		DebugDestroyRemainingTrash();
	}

	if (StepRows.IsValidIndex(CurrentStepIndex) &&
		StepRows[CurrentStepIndex]->StepId == ECSTutorialStepId::PlaceFacility)
	{
		DebugHideTutorialShelfHighlight();
	}

	if (StepRows.IsValidIndex(CurrentStepIndex) &&
		StepRows[CurrentStepIndex]->StepId == ECSTutorialStepId::StockShelves)
	{
		DebugHideTutorialCrateHighlight();
	}

	AdvanceStep();
}

void UCSTutorialSubsystem::DebugSkipTutorial()
{
	// 어느 스텝에서 부르든 안전하게: 남아있을 수 있는 쓰레기/선반/상자 외곽선을 먼저 정리하고 완전히 종료
	DebugDestroyRemainingTrash();
	DebugHideTutorialShelfHighlight();
	DebugHideTutorialCrateHighlight();
	CompleteTutorial();
}

bool UCSTutorialSubsystem::CanCloseOrderWidget()
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return true;
	}

	const ECSTutorialStepId CurrentId = StepRows[CurrentStepIndex]->StepId;

	if (CurrentId == ECSTutorialStepId::PlaceProductOrder && !bOrderWidgetOrderedThisOpen)
	{
		if (IsValid(SubscribedPlayerController))
		{
			SubscribedPlayerController->ShowNoticeBanner(
				NSLOCTEXT("Tutorial", "OrderRequireAtLeastOne", "1개 이상 발주해야 합니다."));
		}
		UpdateHighlightMarkersForCurrentStep();
		return false;
	}

	return true;
}

void UCSTutorialSubsystem::DebugDestroyRemainingTrash()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<ACSTrashActor> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			It->Destroy();
		}
	}
}

void UCSTutorialSubsystem::DebugHideTutorialShelfHighlight()
{
	for (const TObjectPtr<AActor>& Shelf : TutorialSpawnedShelves)
	{
		if (IsValid(Shelf))
		{
			if (UCSTutorialMarkerComponent* HighlightComponent =
				Shelf->FindComponentByClass<UCSTutorialMarkerComponent>())
			{
				HighlightComponent->HideMarker();
			}
		}
	}

	TutorialSpawnedShelves.Reset();
}

void UCSTutorialSubsystem::DebugHideTutorialCrateHighlight()
{
	for (const TObjectPtr<AActor>& Crate : TutorialSpawnedCrates)
	{
		if (IsValid(Crate))
		{
			if (UCSTutorialMarkerComponent* HighlightComponent =
				Crate->FindComponentByClass<UCSTutorialMarkerComponent>())
			{
				HighlightComponent->HideMarker();
			}
		}
	}

	TutorialSpawnedCrates.Reset();
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

		// AddUniqueDynamic + AddUnique: 이 함수가 나중에 두 번 불려도(재구독 등)
		// 같은 쓰레기통에 핸들러가 중복으로 걸리지 않도록 방어
		TrashCan->OnItemDisposed.AddUniqueDynamic(this, &UCSTutorialSubsystem::HandleTrashDisposed);
		SubscribedTrashCans.AddUnique(TrashCan);
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

void UCSTutorialSubsystem::SubscribeToLevelShelves()
{
	if (GetWorld() == nullptr)
	{
		return;
	}

	// 레벨에 배치된 진열대를 전부 찾아 구독
	// 진열대는 튜토리얼의 존재를 모르므로 서브시스템이 먼저 찾아가서 연결
	for (TActorIterator<ACSShelfActor> It(GetWorld()); It; ++It)
	{
		ACSShelfActor* Shelf = *It;
		if (!IsValid(Shelf))
		{
			continue;
		}

		Shelf->OnItemPlaced.AddUniqueDynamic(this, &UCSTutorialSubsystem::HandleShelfItemPlaced);
		SubscribedShelves.AddUnique(Shelf);
	}
}

void UCSTutorialSubsystem::UnsubscribeFromLevelShelves()
{
	for (ACSShelfActor* Shelf : SubscribedShelves)
	{
		if (IsValid(Shelf))
		{
			Shelf->OnItemPlaced.RemoveDynamic(this, &UCSTutorialSubsystem::HandleShelfItemPlaced);
		}
	}

	SubscribedShelves.Reset();
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
	ACSPlayerCharacter* PlayerCharacter = nullptr;
	if (IsValid(PlayerController))
	{
		// Character 준비 여부와 무관하게 기존 UI 열림/닫힘 이벤트는 즉시 구독합니다.
		SubscribeToPlayerController(PlayerController);
		PlayerCharacter = Cast<ACSPlayerCharacter>(PlayerController->GetPawn());
	}

	if (IsValid(PlayerCharacter))
	{
		PlayerControllerSubscribeAttempts = 0;
		SubscribeToPlayerCharacter(PlayerCharacter);
		return;
	}

	// 아직 컨트롤러 또는 플레이어 캐릭터가 준비되지 않음 -> 기존 짧은 재시도 흐름을 사용
	// 정상적인 경우 한두 번 안에 찾아지지만 혹시 모를 상황을 대비해 재시도 횟수를 제한
	++PlayerControllerSubscribeAttempts;
	if (PlayerControllerSubscribeAttempts > MaxPlayerControllerSubscribeAttempts)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Tutorial] PlayerController/PlayerCharacter를 %d회 시도해도 찾지 못해 구독을 포기합니다."),
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

	PlayerController->OnGameplayUIInputLockChanged.AddUniqueDynamic(
		this, &UCSTutorialSubsystem::HandleGameplayUIInputLockChanged);
	PlayerController->OnStoreOpenedAnnouncementFinished.AddUniqueDynamic(
		this, &UCSTutorialSubsystem::HandleStoreOpenedAnnouncementFinished);
	PlayerController->OnStoreClosedAnnouncementFinished.AddUniqueDynamic(
		this, &UCSTutorialSubsystem::HandleStoreClosedAnnouncementFinished);
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
		SubscribedPlayerController->OnStoreOpenedAnnouncementFinished.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleStoreOpenedAnnouncementFinished);
		SubscribedPlayerController->OnStoreClosedAnnouncementFinished.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleStoreClosedAnnouncementFinished);
	}

	SubscribedPlayerController = nullptr;
}

void UCSTutorialSubsystem::SubscribeToOrderSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UCSOrderSubsystem* OrderSubsystem = World->GetSubsystem<UCSOrderSubsystem>())
		{
			SubscribedOrderSubsystem = OrderSubsystem;
			OrderSubsystem->OnOrderPlaced.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleOrderPlaced);
		}
	}
}

void UCSTutorialSubsystem::UnsubscribeFromOrderSubsystem()
{
	if (IsValid(SubscribedOrderSubsystem))
	{
		SubscribedOrderSubsystem->OnOrderPlaced.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleOrderPlaced);
	}

	SubscribedOrderSubsystem = nullptr;
}

void UCSTutorialSubsystem::SubscribeToStoreGameState()
{
	if (UWorld* World = GetWorld())
	{
		if (ACSStoreGameState* StoreGameState = World->GetGameState<ACSStoreGameState>())
		{
			SubscribedStoreGameState = StoreGameState;
			StoreGameState->OnStoreOpenStateChanged.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleStoreOpenStateChanged);
			StoreGameState->OnStorePhaseChanged.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleStorePhaseChanged);
		}
	}
}

void UCSTutorialSubsystem::UnsubscribeFromStoreGameState()
{
	if (IsValid(SubscribedStoreGameState))
	{
		SubscribedStoreGameState->OnStoreOpenStateChanged.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleStoreOpenStateChanged);
		SubscribedStoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleStorePhaseChanged);
	}

	SubscribedStoreGameState = nullptr;
}

void UCSTutorialSubsystem::SubscribeToCustomerRuntimeSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem = World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			SubscribedCustomerRuntimeSubsystem = CustomerRuntimeSubsystem;
			CustomerRuntimeSubsystem->OnInnerThoughtShown.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleInnerThoughtShown);
			CustomerRuntimeSubsystem->OnHeadIconShown.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleHeadIconShown);
		}
	}
}

void UCSTutorialSubsystem::UnsubscribeFromCustomerRuntimeSubsystem()
{
	if (IsValid(SubscribedCustomerRuntimeSubsystem))
	{
		SubscribedCustomerRuntimeSubsystem->OnInnerThoughtShown.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleInnerThoughtShown);
		SubscribedCustomerRuntimeSubsystem->OnHeadIconShown.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleHeadIconShown);
	}

	SubscribedCustomerRuntimeSubsystem = nullptr;
}

void UCSTutorialSubsystem::SubscribeToPlayerCharacter(ACSPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter))
	{
		return;
	}

	if (SubscribedPlayerCharacter != PlayerCharacter)
	{
		UnsubscribeFromPlayerCharacter();
		SubscribedPlayerCharacter = PlayerCharacter;
	}

	// PlayerCharacter는 범용 성공 이벤트만 방송하고, 튜토리얼 4~5단계 완료 판단은 여기서 담당합니다.
	PlayerCharacter->OnBuildModeChanged.AddUniqueDynamic(
		this, &UCSTutorialSubsystem::HandleBuildModeChanged);
	PlayerCharacter->OnBuildableFacilityPlaced.AddUniqueDynamic(
		this, &UCSTutorialSubsystem::HandleBuildableFacilityPlaced);
}

void UCSTutorialSubsystem::UnsubscribeFromPlayerCharacter()
{
	if (IsValid(SubscribedPlayerCharacter))
	{
		SubscribedPlayerCharacter->OnBuildModeChanged.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleBuildModeChanged);
		SubscribedPlayerCharacter->OnBuildableFacilityPlaced.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleBuildableFacilityPlaced);
	}

	SubscribedPlayerCharacter = nullptr;
}

void UCSTutorialSubsystem::HandleBuildModeChanged(bool bIsBuildMode)
{
	if (!bIsBuildMode ||
		!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::EnterBuildMode)
	{
		return;
	}

	// 4단계는 범용 Build Mode 진입 성공 이벤트를 받았을 때만 완료합니다.
	NotifyStepCompleted(ECSTutorialStepId::EnterBuildMode);
}

void UCSTutorialSubsystem::HandleBuildableFacilityPlaced(AActor* FacilityActor)
{
	if (!IsValid(FacilityActor))
	{
		return;
	}

	// 건축으로 새로 배치된 시설이 쓰레기통이면 튜토리얼 시작 시점의 1회 순회
	// 지금이 몇 단계든 상관없이 처리해야 하므로 아래 PlaceFacility 스텝 판정보다 먼저
	if (ACSTrashCanActor* TrashCan = Cast<ACSTrashCanActor>(FacilityActor))
	{
		TrashCan->OnItemDisposed.AddUniqueDynamic(this, &UCSTutorialSubsystem::HandleTrashDisposed);
		SubscribedTrashCans.AddUnique(TrashCan);
	}

	// 같은 이유로 새로 배치된 진열대도 여기서 구독을 걸어준다
	if (ACSShelfActor* Shelf = Cast<ACSShelfActor>(FacilityActor))
	{
		Shelf->OnItemPlaced.AddUniqueDynamic(this, &UCSTutorialSubsystem::HandleShelfItemPlaced);
		SubscribedShelves.AddUnique(Shelf);
	}

	if (!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::PlaceFacility)
	{
		return;
	}

	// 지금 배치된 게 튜토리얼이 스폰해둔 선반들 중 하나가 아니면 무시
	// (건축모드 중 다른 시설을 배치했을 가능성을 배제)
	if (!TutorialSpawnedShelves.Contains(FacilityActor))
	{
		return;
	}

	// 정식 배치가 확정된 시점이므로 튜토리얼이 스폰 시점에 켜뒀던 강조 표시를 여기서 끔
	if (UCSTutorialMarkerComponent* HighlightComponent =
		FacilityActor->FindComponentByClass<UCSTutorialMarkerComponent>())
	{
		HighlightComponent->HideMarker();
	}
	TutorialSpawnedShelves.Remove(FacilityActor);

	if (PlacedFacilityCount < MAX_int32)
	{
		++PlacedFacilityCount;
	}

	// 5단계는 배치할 선반이 몇 개든 이 카운터가 ObjectiveGoal에 도달할 때 완료
	NotifyObjectiveProgress(ECSTutorialStepId::PlaceFacility, PlacedFacilityCount);
}

void UCSTutorialSubsystem::HandleOrderPlaced(const FCSOrderResult& Result)
{
	(void)Result;   // 이 델리게이트는 발주 성공 시에만 방송되므로 별도 성공 여부 검사가 필요 없음

	// 발주창을 안 닫고 계속 열어둔 채여도 "이번에 열려 있는 동안 적어도 1번 발주했다"를 기록
	// 스텝 완료는 여기서 바로 하지 않음 - 발주창이 열려 있는 동안 다음 스텝(장부) 안내가
	// 뜨면 안 되므로, 실제 완료 판정은 창이 닫힐 때(HandleOrderWidgetLockChanged)로 미룸
	bOrderWidgetOrderedThisOpen = true;
}

void UCSTutorialSubsystem::HandleStoreOpenStateChanged(bool bNewStoreOpen)
{
	if (!bNewStoreOpen)   // 폐점은 관심 없음 실제로 열릴 때만 반응
	{
		return;
	}

	if (!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::OpenSign)
	{
		return;
	}

	NotifyStepCompleted(ECSTutorialStepId::OpenSign);
}

void UCSTutorialSubsystem::HandleStorePhaseChanged(ECStorePhase NewPhase)
{
	if (NewPhase != ECStorePhase::ClosingFreeRoam || bTutorialCompleted || bPart3EntryScheduled)
	{
		return;
	}

	// 이미 3부에 들어와 있으면 무시 (페이즈 방송이 중복으로 와도 안전)
	if (StepRows.IsValidIndex(CurrentStepIndex) &&
		StepRows[CurrentStepIndex]->Part == ECSTutorialPart::PostClose)
	{
		return;
	}

	if (FindFirstStepIndexForPart(ECSTutorialPart::PostClose) == INDEX_NONE)
	{
		// 3부 스텝이 DT에 없는 구성이면 1부 종료 시점에 이미 CompleteTutorial()로 끝났어야 함
		return;
	}

	// 2부 팁 체인이 아직 진행 중이었다면 여기서 종료
	// 이 시점 이후엔 HandleStoreOpenedAnnouncementFinished가 와도 bTipChainActive 검사로 걸러짐
	bTipChainActive = false;

	// 3부 진입은 "영업시간이 종료되었습니다" 안내가 끝나는 시점(HandleStoreClosedAnnouncementFinished)에 함
	// 여기서 바로 EnterPart3()를 부르면 안내 애니메이션과 겹쳐서 뜸 - bPart3EntryScheduled는 이 함수가
	// 같은 전환에 대해 두 번 반응하지 않게 막는 용도(페이즈 방송 중복 대비)
	bPart3EntryScheduled = true;
}

void UCSTutorialSubsystem::HandleStoreClosedAnnouncementFinished()
{
	if (!bPart3EntryScheduled || bTutorialCompleted)
	{
		return;
	}

	EnterPart3();
}

void UCSTutorialSubsystem::EnterPart3()
{
	const int32 Part3Index = FindFirstStepIndexForPart(ECSTutorialPart::PostClose);
	if (Part3Index == INDEX_NONE)
	{
		return;
	}

	CurrentStepIndex = Part3Index;
	BroadcastCurrentStep();
	StartAutoAdvanceTimerIfNeeded();
	UpdateHighlightMarkersForCurrentStep();
}

void UCSTutorialSubsystem::HandleInnerThoughtShown()
{
	ShowOneShotTip(ECSTutorialStepId::TipInnerThought);
}

void UCSTutorialSubsystem::HandleHeadIconShown(ACSCustomerCharacter* Customer)
{
	(void)Customer;   // 어느 손님인지는 안 봐도 됨 - 팁은 손님 구분 없이 1회만 표시
	ShowOneShotTip(ECSTutorialStepId::TipInnerThought);
}

bool UCSTutorialSubsystem::DoesStepExpectWidget(int32 StepIndex, const UUserWidget* Widget) const
{
	if (!IsValid(Widget) || !StepRows.IsValidIndex(StepIndex))
	{
		return false;
	}

	const FCSTutorialStepRow& Row = *StepRows[StepIndex];
	if (Row.ExpectedWidgetClass.IsNull())
	{
		return false;
	}

	// 필요한 시점에만 실제로 로드 (평소엔 주소만 갖고 있다가 지금 처음 불러옴)
	const UClass* ExpectedClass = Row.ExpectedWidgetClass.LoadSynchronous();
	return ExpectedClass != nullptr && Widget->IsA(ExpectedClass);
}

void UCSTutorialSubsystem::HandleOrderWidgetLockChanged(UUserWidget* RequestingWidget, bool bLocked)
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const ECSTutorialStepId CurrentId = StepRows[CurrentStepIndex]->StepId;
	if (CurrentId != ECSTutorialStepId::PlaceProductOrder && CurrentId != ECSTutorialStepId::FacilityOrderInfo)
	{
		return;
	}

	if (!DoesStepExpectWidget(CurrentStepIndex, RequestingWidget))
	{
		return;
	}

	if (bLocked)
	{
		if (!bOrderWidgetCurrentlyOpen)
		{
			// 최초로 열림 - 발주/시설탭 두 스텝이 공유하는 이 창을 여는 순간이므로 대화창을 치움
			bOrderWidgetCurrentlyOpen = true;
			bOrderWidgetOrderedThisOpen = false;
			ClearTutorialDisplay();
		}
		// [임시] 시설탭 클릭이 이 위젯의 재오픈 신호로 안 와서, FacilityOrderInfo 완료는
		// 당분간 DT의 AutoAdvanceSeconds(5초) 타이머가 대신 맡음. 감지 방법을 찾으면 닫힘 기반으로 되돌릴 것
		return;
	}

	// 여기부터는 닫힘(bLocked == false)
	bOrderWidgetCurrentlyOpen = false;

	if (CurrentId == ECSTutorialStepId::PlaceProductOrder)
	{
		if (!bOrderWidgetOrderedThisOpen)
		{
			// 발주 없이 닫으려고 함 - 완료 처리하지 않고 경고만 띄움. 스텝은 그대로 PlaceProductOrder에 머무름
			if (IsValid(SubscribedPlayerController))
			{
				SubscribedPlayerController->ShowNoticeBanner(
					NSLOCTEXT("Tutorial", "OrderRequireAtLeastOne", "1개 이상 발주해야 합니다."));
			}
			UpdateHighlightMarkersForCurrentStep();
		}
		else
		{
			// 발주는 했고 이제 창을 닫는 시점 - 다음 스텝(장부) 안내는 여기서 처음 뜸
			NotifyStepCompleted(ECSTutorialStepId::PlaceProductOrder);
		}
	}
	// FacilityOrderInfo는 위 [임시] 주석대로 닫힘 기반 완료를 안 함(타이머가 대신 완료시킴)
}

void UCSTutorialSubsystem::HandleGameplayUIInputLockChanged(UUserWidget* RequestingWidget, bool bLocked)
{
	if (!IsValid(RequestingWidget))
	{
		return;
	}

	// 2부 팁 체인(다이어리↔게시판)은 StepRows의 관문형 진행과 별개라 여기서 먼저 처리
	HandleTipWidgetLockChanged(RequestingWidget, bLocked);

	// 발주/시설탭 두 스텝은 같은 창을 계속 열어둔 채 이어지는 흐름이라 전담 함수가 완료 판정까지 다 함
	HandleOrderWidgetLockChanged(RequestingWidget, bLocked);

	// 정산창(C)은 스텝·팁 진행과 무관하게 아무 때나 열 수 있어 위 두 핸들러와 별도로 처리
	HandleDailySummaryWidgetLockChanged(RequestingWidget, bLocked);

	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const FCSTutorialStepRow& CurrentRow = *StepRows[CurrentStepIndex];

	if (CurrentRow.StepId == ECSTutorialStepId::PlaceProductOrder ||
		CurrentRow.StepId == ECSTutorialStepId::FacilityOrderInfo)
	{
		// 위 HandleOrderWidgetLockChanged가 전담하므로 아래 범용 완료 판정에서는 제외
		return;
	}

	if (!DoesStepExpectWidget(CurrentStepIndex, RequestingWidget))
	{
		// 지금 열리거나 닫힌 위젯이 이 스텝이 기다리는 위젯이 아니면 무시
		return;
	}

	// 위젯이 열리는 순간 대화창을 즉시 치움 - 장부/침대(잠자기 확인)처럼 열려 있는 UI 위에
	// 안내 문구가 계속 겹쳐 보이지 않게 함
	if (bLocked)
	{
		ClearTutorialDisplay();
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

void UCSTutorialSubsystem::HandleShelfItemPlaced(AActor* PlacedItem, int32 SlotIndex)
{
	(void)SlotIndex;

	if (!IsValid(PlacedItem) ||
		!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::StockShelves)
	{
		return;
	}

	if (StockShelvesPlacedCount < MAX_int32)
	{
		++StockShelvesPlacedCount;
	}

	NotifyObjectiveProgress(
		ECSTutorialStepId::StockShelves,
		StockShelvesPlacedCount);
}
