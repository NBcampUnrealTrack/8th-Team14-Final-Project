#include "Systems/Tutorial/CSTutorialSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Features/Crafting/CSCraftStationActor.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Tutorial/CSTutorialShelfSpawnPoint.h"
#include "Features/Tutorial/CSTutorialCrateSpawnPoint.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Delivery/CSDeliveryMagicCircleActor.h"
#include "Features/Delivery/CSDeliveryPortalActor.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"
#include "Features/Storage/CSFreshnessSafeZoneActor.h"
#include "Features/Trash/CSTrashActor.h"
#include "Features/Trash/CSTrashCanActor.h"
#include "Features/Tutorial/CSTutorialMarkerComponent.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/Ordering/CSOrderSubsystem.h"
#include "Systems/SaveLoad/CSSaveSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "UI/Journal/CSJournalWidget.h"
#include "UI/Store/CSDailySummaryWidget.h"
#include "UI/Tutorial/CSTutorialBannerWidgetBase.h"
#include "UI/Tutorial/CSTutorialChoiceWidget.h"
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

namespace
{
	// 정리할 때 이 태그로 튜토리얼이 준 물건과 그걸로 만든 결과물을 찾는다
	const FName TutorialCraftItemTag(TEXT("TutorialCraftItem"));
	// 레벨에서 제조대가 있는 건물에 붙여 둔 태그
	const FName TutorialCraftHouseTag(TEXT("TutorialCraftHouse"));
	// 침대가 있는 건물. 침대는 실내라 밖에서 외곽선이 안 보여서 건물을 강조한다
	const FName TutorialHomeHouseTag(TEXT("TutorialHomeHouse"));
	const FName TutorialRetryMessageRow(TEXT("TutorialRetry"));
	// 잎 우산 재료. 첫 제조를 정답 레시피로 안내한다
	const TArray<FName> TutorialIngredientIds = { TEXT("TOI_ElfHerbSoap"), TEXT("SNK_SlimeJelly") };
	// 창고 스텝에서 보내는 상자. 냉장 상품이라 냉장 창고와 신선도 유지를 같이 보여 줄 수 있다
	const TCHAR* StoreCrateProductTablePath = TEXT("/Game/ConvenienceStore/Data/DT_ProductMaster.DT_ProductMaster");
	const FName StoreCrateProductId(TEXT("BEV_SlimePearlMilkTea"));
	// 진열률이 이 아래로 내려가면 품절 팁을 띄운다. 다 비기 전에 채울 시간을 주려는 값
	constexpr float RestockTipFillRatio = 0.25f;
	constexpr float TutorialPollInterval = 0.5f;
	// 젓기 횟수와 안내 문구가 클릭을 늦게 따라오지 않게 짧게 본다
	constexpr float CraftStatePollInterval = 0.2f;
	// 건너뛰기 확인은 제조 선택과 같은 예/아니요 창을 쓴다
	const TCHAR* SkipConfirmWidgetPath = TEXT("/Game/ConvenienceStore/UI/Tutorial/WBP_TutorialChoice.WBP_TutorialChoice_C");
	// 입구 트리거 가장자리에서 이만큼 더 안쪽으로 되돌려야 다시 겹치지 않는다
	constexpr float CraftHousePushBackMargin = 100.0f;
	constexpr double CraftHouseWarningCooldown = 3.0;
	// 배달 마법진 밑 바닥 타일 메시. 맵 액터 이름표는 패키지 빌드에 없어서 메시와 거리로 찾는다
	const FName DeliveryPlaceMeshName(TEXT("Village_Plaza"));
	constexpr double DeliveryPlaceMaxDistance = 1000.0;

	// 배너만 있는 팁은 대화창을 건드리지 않고 배너 줄로만 보낸다
	bool IsBannerOnlyTip(const FCSTutorialStepRow& Row)
	{
		return !Row.BannerText.IsEmpty() && Row.PromptText.IsEmpty() &&
			Row.PromptHintText.IsEmpty() && Row.PromptHintList.IsEmpty();
	}
}

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

	// 불러오기 중에는 복원 전 기본 영업일(1)로 위 검사를 통과하므로 별도로 확인
	// 튜토리얼 진행 상태는 저장하지 않으므로 불러온 월드에서는 시작하지 않음
	if (const UGameInstance* GameInstance = InWorld.GetGameInstance())
	{
		const UCSSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UCSSaveSubsystem>();
		if (SaveSubsystem != nullptr && SaveSubsystem->HasPendingLoad())
		{
			return;
		}
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
		World->GetTimerManager().ClearTimer(StoreCrateCheckTimerHandle);
		World->GetTimerManager().ClearTimer(RestockTipCheckTimerHandle);
		World->GetTimerManager().ClearTimer(CraftStateCheckTimerHandle);
		World->GetTimerManager().ClearTimer(DeliveryCheckTimerHandle);
	}
	HideAllActiveMarkers();
	UnsubscribeFromDeliverySubsystem();
	UnsubscribeFromLevelDisposables();
	UnsubscribeFromLevelShelves();
	UnsubscribeFromPlayerCharacter();
	UnsubscribeFromPlayerController();
	UnsubscribeFromOrderSubsystem();
	UnsubscribeFromStoreGameState();
	UnsubscribeFromCustomerRuntimeSubsystem();
	UnsubscribeFromCraftingSubsystem();
	CloseChoiceWidget();
	CloseSkipConfirmWidget();
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
	bCraftTutorialAccepted = false;
	bSkippingCraftBundle = false;
	bCraftIngredientsPlaced = false;
	bCraftBrewSucceeded = false;
	bDeliveryTutorialActive = false;
	bDeliveryTutorialDone = false;
	DeliveryTutorialOrderId = INDEX_NONE;
	bHasPendingChainTip = false;
	DeferredBannerTipIds.Reset();

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
	SubscribeToCraftingSubsystem();
	SubscribeToDeliverySubsystem();

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

	HandleStepEnded();

	const ECSTutorialPart FinishedPart = StepRows[CurrentStepIndex]->Part;
	int32 NextIndex = CurrentStepIndex + 1;
	if (bSkippingCraftBundle)
	{
		bSkippingCraftBundle = false;
		while (StepRows.IsValidIndex(NextIndex) && IsCraftBundleStep(StepRows[NextIndex]->StepId))
		{
			++NextIndex;
		}
	}

	// 다음 행이 없거나, 있어도 소속 Part가 지금과 다르면 지금 Part는 여기서 끝난 것으로 봄
	// (다음 Part 진입은 HandlePartFinished가 아니라 각 Part의 실제 트리거가 맡음)
	// 이 체크가 없으면 3부 스텝이 1부 바로 다음 행으로 DT에 있을 때 영업 시작과 동시에 떠버림
	if (!StepRows.IsValidIndex(NextIndex) || StepRows[NextIndex]->Part != FinishedPart)
	{
		HandlePartFinished(FinishedPart);
		return;
	}

	EnterStep(NextIndex);
}

void UCSTutorialSubsystem::EnterStep(int32 StepIndex)
{
	if (!StepRows.IsValidIndex(StepIndex))
	{
		return;
	}

	CurrentStepIndex = StepIndex;
	BroadcastCurrentStep();
	StartAutoAdvanceTimerIfNeeded();
	UpdateHighlightMarkersForCurrentStep();
	TrySpawnTutorialShelfDelivery();
	TrySpawnTutorialCrateDeliveries();
	HandleStepStarted();
}

int32 UCSTutorialSubsystem::FindStepIndex(ECSTutorialStepId StepId) const
{
	return StepRows.IndexOfByPredicate([StepId](const FCSTutorialStepRow* Row) { return Row->StepId == StepId; });
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

	// 개점 뒤 첫 배달 주문을 매장에 있는 상품 1개로 받아 배달 튜토리얼에 쓴다
	if (IsValid(SubscribedDeliverySubsystem))
	{
		SubscribedDeliverySubsystem->RequestStockedNextOrder();
	}

	// 진열대가 비어도 손님은 반응 없이 지나가서 이벤트로 잡을 수 없다. 진열률을 직접 본다
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RestockTipCheckTimerHandle, this, &UCSTutorialSubsystem::CheckRestockTip, TutorialPollInterval, true);
	}
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
	if (bDeliveryTutorialActive && IsValid(SubscribedDeliverySubsystem))
	{
		SubscribedDeliverySubsystem->ReleaseGuidedOrder();
	}
	UnsubscribeFromDeliverySubsystem();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeliveryCheckTimerHandle);
	}
	bDeliveryTutorialActive = false;
	CleanUpCraftTutorial();
	UnsubscribeFromCraftingSubsystem();
	CloseSkipConfirmWidget();
	CloseTutorialWidgets();
}

void UCSTutorialSubsystem::BroadcastCurrentStep() const
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const FCSTutorialStepRow& CurrentRow = *StepRows[CurrentStepIndex];
	BroadcastStepRow(CurrentRow);

	// ObjectiveGoal이 0이어도 방송해야 함
	OnTutorialObjectiveProgress.Broadcast(0, CurrentRow.ObjectiveGoal);

	// 정산창(C) 등으로 화면이 잠깐 가려졌다가 그대로 되살아날 때 쓸 최신 상태를 기억해둠
	LastBroadcastRow = &CurrentRow;
	LastBroadcastObjectiveCurrent = 0;
}

void UCSTutorialSubsystem::BroadcastStepRow(const FCSTutorialStepRow& Row) const
{
	if (!CanPlayerSkipCurrentStep())
	{
		OnTutorialStepChanged.Broadcast(Row);
		return;
	}

	FCSTutorialStepRow RowWithSkipHint = Row;
	FCSTutorialHintEntry& SkipHint = RowWithSkipHint.PromptHintList.AddDefaulted_GetRef();
	SkipHint.HintKey = INVTEXT("N");
	SkipHint.HintText = NSLOCTEXT("Tutorial", "SkipStepHint", "이 단계 건너뛰기");
	OnTutorialStepChanged.Broadcast(RowWithSkipHint);
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

	const FCSTutorialStepRow* TipRow = FindTipRow(TipId);
	if (TipRow == nullptr)
	{
		return;
	}

	ShownTipIds.Add(TipId);

	// 배달 안내를 덮지 않게 배달이 끝날 때까지 미룬다
	if (bDeliveryTutorialActive)
	{
		if (IsBannerOnlyTip(*TipRow))
		{
			DeferredBannerTipIds.Add(TipId);
		}
		else
		{
			bHasPendingChainTip = true;
			PendingChainTipId = TipId;
		}
		return;
	}

	DisplayTip(*TipRow);
}

const FCSTutorialStepRow* UCSTutorialSubsystem::FindTipRow(ECSTutorialStepId TipId) const
{
	FCSTutorialStepRow* const* FoundRow = TipRows.FindByPredicate(
		[TipId](const FCSTutorialStepRow* Row) { return Row->StepId == TipId; });
	return FoundRow != nullptr ? *FoundRow : nullptr;
}

void UCSTutorialSubsystem::DisplayTip(const FCSTutorialStepRow& TipRow)
{
	// 배너만 있는 팁을 방송하면 떠 있던 대화창 팁과 마커까지 지워져서 배너로만 보낸다
	if (IsBannerOnlyTip(TipRow) && IsValid(BannerWidgetInstance))
	{
		BannerWidgetInstance->ShowTipBanner(TipRow.BannerText);
		return;
	}

	// 배달 안내에 가려졌다가 끝나면 다시 띄울 수 있게 지금 해야 할 팁으로 기억한다
	bHasPendingChainTip = true;
	PendingChainTipId = TipRow.StepId;

	// 기존 배너/대화창/목표 위젯 구독을 그대로 재사용. 여기서는 CurrentStepIndex를 건드리지 않고
	// 방송만 함. 이전에 켜져 있던 마커는 여기서 새로 켜기 전에 정리
	HideAllActiveMarkers();
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

	// 연습 주문 상품은 튜토리얼 동안 손님이 못 사서, 손님이 막 들어오기 시작할 때 받아야 덜 어색하다.
	// 주문이 오면 배달 튜토리얼이 먼저 뜨고 다이어리 팁은 끝난 뒤로 밀린다
	if (!bDeliveryTutorialDone && IsValid(SubscribedDeliverySubsystem))
	{
		FText IgnoredMessage;
		SubscribedDeliverySubsystem->GenerateOrderNow(IgnoredMessage);
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

	// 배달 중에 팁의 창을 열면 그 팁은 한 것으로 치되, 화면의 배달 안내는 지우지 않는다
	const auto MarkChainTipDone = [this](ECSTutorialStepId TipId)
	{
		if (bHasPendingChainTip && PendingChainTipId == TipId)
		{
			bHasPendingChainTip = false;
		}
		if (!bDeliveryTutorialActive)
		{
			ClearTutorialDisplay();
		}
	};

	if (DoesTipExpectWidget(ECSTutorialStepId::TipDiary, RequestingWidget))
	{
		if (bLocked)
		{
			// 팁이 아직 안 떴어도(안내 애니메이션이 끝나길 대기 중) 다이어리를 직접 열었으면
			// 나중에 안내가 끝나도 다시 뜨지 않도록 이미 본 것으로 표시함
			ShownTipIds.Add(ECSTutorialStepId::TipDiary);
			MarkChainTipDone(ECSTutorialStepId::TipDiary);
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
			MarkChainTipDone(ECSTutorialStepId::TipNoticeBoard);
		}
		else if (!bDeliveryTutorialActive || ShownTipIds.Contains(ECSTutorialStepId::TipNoticeBoard))
		{
			// 게시판을 닫는 순간 정산창 팁으로 넘어감 (이미 봤으면 ShowOneShotTip이 알아서 무시)
			// 배달 수락하러 연 게시판은 다이어리 팁이 남아 있으면 순서를 건너뛰지 않는다
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
		// 정산창 팁은 정산창을 열면 한 것으로 친다. 배달이 끝난 뒤 다시 띄우지 않게
		if (bHasPendingChainTip && PendingChainTipId == ECSTutorialStepId::TipSettlement)
		{
			bHasPendingChainTip = false;
		}

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
		BroadcastStepRow(*LastBroadcastRow);
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

void UCSTutorialSubsystem::NotifyTriggerEntered(ECSTutorialStepId StepId, const AActor* Trigger)
{
	UWorld* World = GetWorld();
	if (StepId != ECSTutorialStepId::CraftMoveToHouse || !bCraftTutorialAccepted || bTutorialCompleted ||
		World == nullptr || !IsValid(Trigger))
	{
		NotifyStepCompleted(StepId);
		return;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	const AActor* Station = IsValid(SubscribedCraftingSubsystem) ? SubscribedCraftingSubsystem->GetCraftStationActor() : nullptr;
	if (!IsValid(PlayerPawn) || !IsValid(Station))
	{
		return;
	}

	// 제조대는 건물 안쪽에 있어서 트리거에서 제조대 쪽으로 밀면 안으로 들어간다
	FVector TriggerOrigin;
	FVector TriggerExtent;
	Trigger->GetActorBounds(false, TriggerOrigin, TriggerExtent);
	const FVector ToInside = (Station->GetActorLocation() - TriggerOrigin).GetSafeNormal2D();
	FVector Target = TriggerOrigin + ToInside * (FMath::Max(TriggerExtent.X, TriggerExtent.Y) + CraftHousePushBackMargin);
	Target.Z = PlayerPawn->GetActorLocation().Z;

	// 겹침 방송 도중에 폰을 옮기면 이동 처리와 꼬여서 다음 틱에 옮긴다
	TWeakObjectPtr<APawn> WeakPawn(PlayerPawn);
	World->GetTimerManager().SetTimerForNextTick([WeakPawn, Target]()
	{
		APawn* Pawn = WeakPawn.Get();
		if (!IsValid(Pawn))
		{
			return;
		}
		Pawn->TeleportTo(Target, Pawn->GetActorRotation());
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		if (UCharacterMovementComponent* Movement = Character != nullptr ? Character->GetCharacterMovement() : nullptr)
		{
			Movement->StopMovementImmediately();
		}
	});

	const double Now = World->GetTimeSeconds();
	if (Now - LastCraftHouseWarningTime >= CraftHouseWarningCooldown && IsValid(BannerWidgetInstance))
	{
		LastCraftHouseWarningTime = Now;
		BannerWidgetInstance->ShowTipBanner(NSLOCTEXT("Tutorial", "CraftHouseExitBlocked", "제조 튜토리얼을 마쳐야 나갈 수 있어요."));
	}
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
	// 선택 창은 예를 고른 것으로 넘긴다. 제조 스텝을 이어서 확인할 수 있게
	if (StepRows.IsValidIndex(CurrentStepIndex) &&
		StepRows[CurrentStepIndex]->StepId == ECSTutorialStepId::CraftChoice)
	{
		HandleTutorialChoiceMade(true);
		return;
	}

	// 제조 스텝은 솥 상태를 따라 되돌아가서, 잎 우산을 만든 것으로 치고 도감 스텝부터 넘긴다
	const int32 RecipeBookIndex = FindStepIndex(ECSTutorialStepId::CraftRecipeBook);
	if (StepRows.IsValidIndex(CurrentStepIndex) && IsCraftBrewPhaseStep(StepRows[CurrentStepIndex]->StepId) &&
		RecipeBookIndex != INDEX_NONE)
	{
		const int32 TargetIndex = bCraftBrewSucceeded ? RecipeBookIndex + 1 : RecipeBookIndex;
		bCraftBrewSucceeded = true;
		HandleStepEnded();
		EnterStep(TargetIndex);
		return;
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
	// 끝난 뒤에 부르면 플레이 중에 생긴 쓰레기까지 지워진다
	if (bTutorialCompleted)
	{
		return;
	}

	// 어느 스텝에서 부르든 안전하게: 남아있을 수 있는 쓰레기/선반/상자 외곽선을 먼저 정리하고 완전히 종료
	DestroyRemainingTrash();
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
		// 돈이 없어 못 산 경우면 경고 대신 지원금 안내가 뜬다
		// 공용 알림은 발주창 아래 층에 떠서 튜토리얼 배너로 띄운다
		if (!GrantTutorialOrderFundsIfNeeded() && IsValid(BannerWidgetInstance))
		{
			BannerWidgetInstance->ShowTipBanner(NSLOCTEXT("Tutorial", "OrderRequireAtLeastOne", "1개 이상 발주해야 합니다."));
		}
		UpdateHighlightMarkersForCurrentStep();
		return false;
	}

	return true;
}

void UCSTutorialSubsystem::ShowCurrentStepWithoutBanner()
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	FCSTutorialStepRow Row = *StepRows[CurrentStepIndex];
	Row.BannerText = FText::GetEmpty();
	BroadcastStepRow(Row);
	OnTutorialObjectiveProgress.Broadcast(0, Row.ObjectiveGoal);
	LastBroadcastRow = StepRows[CurrentStepIndex];
	LastBroadcastObjectiveCurrent = 0;
	UpdateHighlightMarkersForCurrentStep();
}

bool UCSTutorialSubsystem::GrantTutorialOrderFundsIfNeeded()
{
	UWorld* World = GetWorld();
	ACSStoreGameState* State = World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	UDataTable* ProductTable = LoadObject<UDataTable>(nullptr, StoreCrateProductTablePath);
	if (!IsValid(State) || !IsValid(SubscribedOrderSubsystem) || !IsValid(ProductTable))
	{
		return false;
	}

	// 발주 스텝은 1건 이상 발주해야 끝나서 돈이 없으면 튜토리얼이 멈춘다
	int32 CheapestCost = MAX_int32;
	for (const FName& RowName : ProductTable->GetRowNames())
	{
		FCSOrderLine Line;
		Line.ProductRow.DataTable = ProductTable;
		Line.ProductRow.RowName = RowName;
		Line.CrateCount = 1;
		if (!SubscribedOrderSubsystem->IsProductUnlocked(Line.ProductRow))
		{
			continue;
		}

		const FCSOrderResult Result = SubscribedOrderSubsystem->EvaluateOrder({ Line });
		if (Result.bSuccess)
		{
			return false;
		}
		if (Result.FailureReason == ECOrderFailureReason::InsufficientFunds)
		{
			CheapestCost = FMath::Min(CheapestCost, Result.TotalCost);
		}
	}

	const int32 Grant = CheapestCost == MAX_int32 ? 0 : CheapestCost - State->GetCurrentFunds();
	if (Grant <= 0 || !State->AddFunds(Grant))
	{
		return false;
	}

	// 공용 알림은 발주창 아래 층에 떠서 튜토리얼 배너로 띄운다
	if (IsValid(BannerWidgetInstance))
	{
		BannerWidgetInstance->ShowTipBanner(FText::Format(
			NSLOCTEXT("Tutorial", "OrderFundsGranted", "튜토리얼 지원금 {0}G를 드렸어요."),
			FText::AsNumber(Grant)));
	}
	return true;
}

void UCSTutorialSubsystem::DestroyRemainingTrash()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	ACSPlayerCharacter* PlayerCharacter = PlayerController != nullptr ? Cast<ACSPlayerCharacter>(PlayerController->GetPawn()) : nullptr;
	for (TActorIterator<ACSTrashActor> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			// 들고 있던 쓰레기를 손과 퀵바에서 먼저 떼야 지운 뒤 퀵바 아이콘이 남지 않는다
			if (IsValid(PlayerCharacter))
			{
				PlayerCharacter->RemoveProductFromHands(*It);
			}
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

	if (!IsValid(SkipInputComponent))
	{
		SkipInputComponent = NewObject<UInputComponent>(PlayerController);
		SkipInputComponent->BindKey(EKeys::N, IE_Pressed, this, &UCSTutorialSubsystem::HandleSkipKeyPressed);
		PlayerController->PushInputComponent(SkipInputComponent);
	}
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
		if (IsValid(SkipInputComponent))
		{
			SubscribedPlayerController->PopInputComponent(SkipInputComponent);
		}
	}

	SkipInputComponent = nullptr;
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
	// 3부 도중 다음 날로 넘어가면 안내와 튜토리얼 물건이 2일차까지 남는다.
	// 1일차 시작에도 Preparation이 오므로 3부에 들어선 뒤에만 끝낸다
	if (NewPhase == ECStorePhase::Preparation)
	{
		const bool bInPart3 = bPart3EntryScheduled ||
			(StepRows.IsValidIndex(CurrentStepIndex) && StepRows[CurrentStepIndex]->Part == ECSTutorialPart::PostClose);
		if (!bTutorialCompleted && bInPart3)
		{
			CompleteTutorial();
		}
		return;
	}

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

	// 마감으로 배달 주문은 실패한다. 남은 2부 팁은 3부에 띄우지 않는다
	if (bDeliveryTutorialActive)
	{
		FinishDeliveryTutorial(false);
	}

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

	EnterStep(Part3Index);
}

void UCSTutorialSubsystem::HandleInnerThoughtShown()
{
	// 배달 중에는 아예 무시한다. 본 것으로 치지 않아서 끝난 뒤 다음 손님 아이콘에 뜬다
	if (!bDeliveryTutorialActive)
	{
		ShowOneShotTip(ECSTutorialStepId::TipInnerThought);
	}
}

void UCSTutorialSubsystem::HandleHeadIconShown(ACSCustomerCharacter* Customer)
{
	(void)Customer;   // 어느 손님인지는 안 봐도 됨 - 팁은 손님 구분 없이 1회만 표시
	HandleInnerThoughtShown();
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
			if (CurrentId == ECSTutorialStepId::PlaceProductOrder)
			{
				GrantTutorialOrderFundsIfNeeded();
			}
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
			// 창을 열 때 지운 안내를 다시 띄우지 않으면 튜토리얼이 끝난 것처럼 보인다
			ShowCurrentStepWithoutBanner();
			if (!GrantTutorialOrderFundsIfNeeded() && IsValid(BannerWidgetInstance))
			{
				BannerWidgetInstance->ShowTipBanner(NSLOCTEXT("Tutorial", "OrderRequireAtLeastOne", "1개 이상 발주해야 합니다."));
			}
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

	// 대화창과 목표가 다른 창의 버튼을 가려서 창이 하나라도 열려 있으면 숨긴다.
	// BP가 내용이 바뀔 때마다 표시 상태를 다시 정해서 표시 상태 대신 투명도로 숨긴다
	const float PanelOpacity =
		IsValid(SubscribedPlayerController) && SubscribedPlayerController->IsGameplayUIInputLocked() ? 0.0f : 1.0f;
	if (IsValid(DialogueWidgetInstance))
	{
		DialogueWidgetInstance->SetRenderOpacity(PanelOpacity);
	}
	if (IsValid(ObjectiveWidgetInstance))
	{
		ObjectiveWidgetInstance->SetRenderOpacity(PanelOpacity);
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

	// 도감은 솥 클로즈업에서만 Tab으로 열려서 밖에서 누른 Tab은 다이어리를 연다
	UWorld* World = GetWorld();
	if (bLocked && bCraftBrewSucceeded && IsCraftBrewPhaseStep(StepRows[CurrentStepIndex]->StepId) &&
		Cast<UCSJournalWidget>(RequestingWidget) != nullptr && World != nullptr)
	{
		// 다이어리를 여는 도중에 온 방송이라 바로 닫으면 컨트롤러가 비운 위젯에 포커스를 준다
		TWeakObjectPtr<ACSStorePlayerController> WeakController(SubscribedPlayerController);
		TWeakObjectPtr<UCSTutorialBannerWidgetBase> WeakBanner(BannerWidgetInstance);
		World->GetTimerManager().SetTimerForNextTick([WeakController, WeakBanner]()
		{
			if (WeakController.IsValid())
			{
				WeakController->CloseJournal();
			}
			if (WeakBanner.IsValid())
			{
				WeakBanner->ShowTipBanner(NSLOCTEXT("Tutorial", "RecipeBookNeedsCloseup",
					"도감은 솥을 [E]로 살펴보는 중에\n[Tab]으로 열 수 있어요."));
			}
		});
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

bool UCSTutorialSubsystem::ShouldBlockSleep(FText& OutMessage) const
{
	if (bTutorialCompleted || !StepRows.IsValidIndex(CurrentStepIndex))
	{
		return false;
	}

	// 마감 안내가 끝나기 전에는 현재 스텝이 아직 1부 행이라 예약 여부도 같이 본다
	const FCSTutorialStepRow& Row = *StepRows[CurrentStepIndex];
	const bool bInPart3 = bPart3EntryScheduled || Row.Part == ECSTutorialPart::PostClose;
	if (!bInPart3 || Row.StepId == ECSTutorialStepId::ApproachClosingBell)
	{
		return false;
	}

	OutMessage = Row.Part == ECSTutorialPart::PostClose && !Row.SleepBlockedText.IsEmpty()
		? Row.SleepBlockedText
		: NSLOCTEXT("Tutorial", "SleepBlocked", "아직 오늘 할 일이 남아 있어요.");
	return true;
}

bool UCSTutorialSubsystem::ShouldBlockStoreOpen(FText& OutMessage) const
{
	if (bTutorialCompleted || !StepRows.IsValidIndex(CurrentStepIndex))
	{
		return false;
	}

	// 1부 중간에 개점하면 남은 준비 스텝이 영업 중에 뜬다
	const FCSTutorialStepRow& Row = *StepRows[CurrentStepIndex];
	if (Row.Part != ECSTutorialPart::PreOpen || Row.StepId == ECSTutorialStepId::OpenSign)
	{
		return false;
	}

	OutMessage = NSLOCTEXT("Tutorial", "StoreOpenBlocked",
		"튜토리얼 중에는 영업을 시작할 수 없습니다.\n막힌 단계는 [N]으로 건너뛸 수 있어요.");
	return true;
}

bool UCSTutorialSubsystem::IsCraftBundleStep(ECSTutorialStepId StepId)
{
	// 선택 창에서 아니요를 고르면 이 스텝들을 시작하지 않고 건너뛴다
	return IsCraftBrewPhaseStep(StepId) ||
		StepId == ECSTutorialStepId::CraftAutoCraft ||
		StepId == ECSTutorialStepId::CraftWrapUp;
}

bool UCSTutorialSubsystem::IsCraftBrewPhaseStep(ECSTutorialStepId StepId)
{
	return StepId == ECSTutorialStepId::CraftAddIngredients ||
		StepId == ECSTutorialStepId::CraftCloseup ||
		StepId == ECSTutorialStepId::CraftHeat ||
		StepId == ECSTutorialStepId::CraftStir ||
		StepId == ECSTutorialStepId::CraftBrew ||
		StepId == ECSTutorialStepId::CraftRecipeBook;
}

ECSTutorialStepId UCSTutorialSubsystem::ResolveCraftStep(
	bool bBrewDone, bool bPotReady, bool bCloseup, ECSCraftStep CraftStep, bool bHeatCorrect, bool bStirDone)
{
	if (bBrewDone)
	{
		return bCloseup ? ECSTutorialStepId::CraftRecipeBook : ECSTutorialStepId::CraftCloseup;
	}
	// 제조가 시작되면 재료가 잠겨서 재료 수는 젓기 전까지만 본다
	if ((CraftStep == ECSCraftStep::Idle || CraftStep == ECSCraftStep::Heat) && !bPotReady)
	{
		return ECSTutorialStepId::CraftAddIngredients;
	}
	if (!bCloseup)
	{
		return ECSTutorialStepId::CraftCloseup;
	}
	if (CraftStep == ECSCraftStep::Idle || (CraftStep == ECSCraftStep::Heat && !bHeatCorrect))
	{
		return ECSTutorialStepId::CraftHeat;
	}
	if (CraftStep == ECSCraftStep::Heat || (CraftStep == ECSCraftStep::Stir && !bStirDone))
	{
		return ECSTutorialStepId::CraftStir;
	}
	return ECSTutorialStepId::CraftBrew;
}

void UCSTutorialSubsystem::CheckCraftTutorialState()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (bTutorialCompleted || !StepRows.IsValidIndex(CurrentStepIndex) ||
		!IsCraftBundleStep(StepRows[CurrentStepIndex]->StepId))
	{
		World->GetTimerManager().ClearTimer(CraftStateCheckTimerHandle);
		return;
	}

	const ACSCraftStationActor* Station = IsValid(SubscribedCraftingSubsystem)
		? Cast<ACSCraftStationActor>(SubscribedCraftingSubsystem->GetCraftStationActor())
		: nullptr;
	if (!IsValid(Station))
	{
		return;
	}

	// 마무리 안내는 제조 화면에서 나오면 끝난다. 자동 제조 스텝은 창이 닫힐 때 넘어간다
	const ECSTutorialStepId CurrentId = StepRows[CurrentStepIndex]->StepId;
	if (CurrentId == ECSTutorialStepId::CraftWrapUp && !Station->IsCloseupActive())
	{
		NotifyStepCompleted(ECSTutorialStepId::CraftWrapUp);
		return;
	}
	if (!IsCraftBrewPhaseStep(CurrentId))
	{
		return;
	}

	const FCSCraftRecipeRow* Recipe = SubscribedCraftingSubsystem->FindRecipe(TutorialIngredientIds[0], TutorialIngredientIds[1]);
	if (Recipe == nullptr)
	{
		return;
	}

	int32 PotCount = 0;
	for (const FCSCraftIngredientEntry& Entry : Station->GetIngredientEntries())
	{
		PotCount += Entry.Count;
	}
	const ECSCraftStep CraftStep = Station->GetCraftStep();
	const int32 StirCount = CraftStep == ECSCraftStep::Stir ? Station->GetStirCount() : 0;
	const ECSTutorialStepId TargetId = ResolveCraftStep(
		bCraftBrewSucceeded,
		PotCount >= TutorialIngredientIds.Num(),
		Station->IsCloseupActive(),
		CraftStep,
		Station->GetCurrentHeat() == Recipe->Heat,
		StirCount >= Recipe->StirCount);

	if (TargetId != StepRows[CurrentStepIndex]->StepId)
	{
		const int32 TargetIndex = FindStepIndex(TargetId);
		if (TargetIndex != INDEX_NONE)
		{
			HandleStepEnded();
			EnterStep(TargetIndex);
		}
		return;
	}

	// 재료 수와 젓기 횟수는 목표 칸에 바로 보여 준다
	const int32 Progress = TargetId == ECSTutorialStepId::CraftAddIngredients ? PotCount
		: TargetId == ECSTutorialStepId::CraftStir ? StirCount
		: INDEX_NONE;
	if (Progress != INDEX_NONE && Progress != LastBroadcastObjectiveCurrent)
	{
		OnTutorialObjectiveProgress.Broadcast(Progress, StepRows[CurrentStepIndex]->ObjectiveGoal);
		LastBroadcastObjectiveCurrent = Progress;
	}
}

FText UCSTutorialSubsystem::CheckCraftInput(ECSCraftInput Input) const
{
	if (bTutorialCompleted || !StepRows.IsValidIndex(CurrentStepIndex) ||
		!IsCraftBrewPhaseStep(StepRows[CurrentStepIndex]->StepId) || !IsValid(SubscribedCraftingSubsystem))
	{
		return FText::GetEmpty();
	}
	const ACSCraftStationActor* Station = Cast<ACSCraftStationActor>(SubscribedCraftingSubsystem->GetCraftStationActor());
	const FCSCraftRecipeRow* Recipe = SubscribedCraftingSubsystem->FindRecipe(TutorialIngredientIds[0], TutorialIngredientIds[1]);
	if (!IsValid(Station) || Recipe == nullptr)
	{
		return FText::GetEmpty();
	}

	// 문구의 약불과 세 번은 잎 우산 레시피 값이다. DT 안내 문구와 같이 고친다
	const ECSCraftStep CraftStep = Station->GetCraftStep();
	switch (Input)
	{
	case ECSCraftInput::Stir:
		// 완성품이 결과 자리에 있어서 다시 저으면 결과물을 치우라는 안내만 뜬다
		if (bCraftBrewSucceeded && CraftStep == ECSCraftStep::Heat)
		{
			return NSLOCTEXT("Tutorial", "CraftBlockStirAfterBrew", "[Tab]으로 도감을 먼저 열어 보세요.");
		}
		if (CraftStep == ECSCraftStep::Heat && Station->GetCurrentHeat() != Recipe->Heat)
		{
			return NSLOCTEXT("Tutorial", "CraftBlockHeat", "잎 우산은 약불이에요. 불을 약불로 맞춘 뒤 저어요.");
		}
		if (CraftStep == ECSCraftStep::Stir && Station->GetStirCount() >= Recipe->StirCount)
		{
			return NSLOCTEXT("Tutorial", "CraftBlockOverStir", "충분히 저었어요. 이제 거품을 터트려요.");
		}
		break;
	case ECSCraftInput::Pop:
		if (CraftStep == ECSCraftStep::Stir && Station->GetStirCount() < Recipe->StirCount)
		{
			return NSLOCTEXT("Tutorial", "CraftBlockEarlyPop", "세 번 다 저은 뒤에 거품을 터트려요.");
		}
		break;
	case ECSCraftInput::FinishWithMissedPops:
		// 재료를 다시 주고 처음부터 시키면 이미 한 불과 젓기를 또 해야 한다
		return NSLOCTEXT("Tutorial", "CraftRetryPops", "거품이 가장 커져 금색이 될 때 [Space]로 다시 터트려 보세요.");
	case ECSCraftInput::RecipeBook:
		if (!bCraftBrewSucceeded)
		{
			return NSLOCTEXT("Tutorial", "CraftBlockBook", "도감은 잎 우산을 만든 뒤에 열어 봐요.");
		}
		break;
	case ECSCraftInput::AutoCraft:
		return NSLOCTEXT("Tutorial", "CraftBlockAuto", "지금은 직접 만들어 봐요.");
	case ECSCraftInput::IngredientPanel:
		return NSLOCTEXT("Tutorial", "CraftBlockIngredientPanel", "지금은 재료를 꺼내지 않아도 돼요.");
	default:
		break;
	}
	return FText::GetEmpty();
}

void UCSTutorialSubsystem::HandleStepStarted()
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	const FCSTutorialStepRow& Row = *StepRows[CurrentStepIndex];
	auto FindTaggedActors = [this](FName Tag)
	{
		TArray<AActor*> Found;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (IsValid(*It) && It->ActorHasTag(Tag))
				{
					Found.Add(*It);
				}
			}
		}
		return Found;
	};

	switch (Row.StepId)
	{
	case ECSTutorialStepId::CraftMoveToHouse:
		for (AActor* House : FindTaggedActors(TutorialCraftHouseTag))
		{
			AttachCraftMarker(House, Row.MarkerLabelText);
		}
		break;
	case ECSTutorialStepId::ApproachClosingBell:
		// 라벨은 침대 마커가 이미 띄우므로 건물은 외곽선만
		for (AActor* House : FindTaggedActors(TutorialHomeHouseTag))
		{
			AttachCraftMarker(House, FText::GetEmpty());
		}
		break;
	case ECSTutorialStepId::CraftChoice:
		OpenChoiceWidget(Row);
		break;
	case ECSTutorialStepId::StoreCrate:
		BeginStoreCrateStep();
		break;
	case ECSTutorialStepId::CraftAddIngredients:
		// 실패하거나 재료를 다시 꺼내 돌아왔을 때는 이미 놓인 재료에 마커만 붙인다
		if (!bCraftIngredientsPlaced)
		{
			PlaceTutorialIngredients();
			bCraftIngredientsPlaced = true;
		}
		for (TActorIterator<ACSProductPickupActor> It(GetWorld()); It; ++It)
		{
			// 솥에 들어간 재료는 숨겨져 있다
			if (IsValid(*It) && It->ActorHasTag(TutorialCraftItemTag) && !It->IsHidden())
			{
				AttachCraftMarker(*It, FText::GetEmpty());
			}
		}
		if (IsValid(SubscribedCraftingSubsystem))
		{
			AttachCraftMarker(SubscribedCraftingSubsystem->GetCraftStationActor(), FText::GetEmpty());
			// 튜토리얼 대화창 힌트가 클로즈업 키 안내·정보 창과 같은 자리에 겹친다
			SubscribedCraftingSubsystem->SetCraftCloseupGuidesHidden(true);
		}
		break;
	case ECSTutorialStepId::CraftCloseup:
		if (IsValid(SubscribedCraftingSubsystem))
		{
			AttachCraftMarker(SubscribedCraftingSubsystem->GetCraftStationActor(), Row.MarkerLabelText);
		}
		break;
	default:
		break;
	}

	UWorld* World = GetWorld();
	if (IsCraftBundleStep(Row.StepId) && World != nullptr &&
		!World->GetTimerManager().IsTimerActive(CraftStateCheckTimerHandle))
	{
		World->GetTimerManager().SetTimer(
			CraftStateCheckTimerHandle, this, &UCSTutorialSubsystem::CheckCraftTutorialState, CraftStatePollInterval, true);
	}
	if (IsCraftBundleStep(Row.StepId) && IsValid(SubscribedCraftingSubsystem) &&
		!SubscribedCraftingSubsystem->CraftInputGate.IsBound())
	{
		SubscribedCraftingSubsystem->CraftInputGate.BindUObject(this, &UCSTutorialSubsystem::CheckCraftInput);
	}
}

void UCSTutorialSubsystem::HandleStepEnded()
{
	if (!StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	switch (StepRows[CurrentStepIndex]->StepId)
	{
	case ECSTutorialStepId::CleanTrash:
		// 마지막 쓰레기를 버리는 방송 도중이라 쓰레기통과 플레이어가 그 쓰레기를 다 정리한 다음 틱에 지운다
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &UCSTutorialSubsystem::DestroyRemainingTrash);
		}
		break;
	case ECSTutorialStepId::CraftMoveToHouse:
	case ECSTutorialStepId::CraftAddIngredients:
	case ECSTutorialStepId::CraftCloseup:
	case ECSTutorialStepId::ApproachClosingBell:
		RemoveCraftMarkers();
		break;
	case ECSTutorialStepId::CraftChoice:
		CloseChoiceWidget();
		break;
	case ECSTutorialStepId::StoreCrate:
		// cs.TNext로 건너뛸 때도 확인 타이머와 마커가 남지 않게 여기서 정리한다
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(StoreCrateCheckTimerHandle);
		}
		RemoveCraftMarkers();
		break;
	case ECSTutorialStepId::CraftRecipeBook:
		// 만든 잎 우산을 회수한다. 도감을 본 뒤라 결과 슬롯을 비워도 된다
		if (IsValid(SubscribedCraftingSubsystem))
		{
			SubscribedCraftingSubsystem->ClearCraftStationPot();
		}
		break;
	case ECSTutorialStepId::CraftWrapUp:
		CleanUpCraftTutorial();
		break;
	default:
		break;
	}
}

void UCSTutorialSubsystem::SubscribeToCraftingSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UCSCraftingSubsystem* CraftingSubsystem = World->GetSubsystem<UCSCraftingSubsystem>())
		{
			SubscribedCraftingSubsystem = CraftingSubsystem;
			CraftingSubsystem->OnCraftIngredientAdded.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleCraftIngredientAdded);
			CraftingSubsystem->OnManualCraftFinished.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleManualCraftFinished);
		}
	}
}

void UCSTutorialSubsystem::UnsubscribeFromCraftingSubsystem()
{
	if (IsValid(SubscribedCraftingSubsystem))
	{
		SubscribedCraftingSubsystem->OnCraftIngredientAdded.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleCraftIngredientAdded);
		SubscribedCraftingSubsystem->OnManualCraftFinished.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleManualCraftFinished);
		SubscribedCraftingSubsystem->CraftInputGate.Unbind();
	}

	SubscribedCraftingSubsystem = nullptr;
}

void UCSTutorialSubsystem::OpenChoiceWidget(const FCSTutorialStepRow& Row)
{
	CloseChoiceWidget();

	ACSStorePlayerController* PlayerController = IsValid(SubscribedPlayerController)
		? SubscribedPlayerController.Get()
		: Cast<ACSStorePlayerController>(GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr);
	UClass* ChoiceClass = Row.ChoiceWidgetClass.LoadSynchronous();
	if (!IsValid(PlayerController) || ChoiceClass == nullptr)
	{
		// 창 없이 멈춰 있으면 튜토리얼이 진행되지 않아서 예로 넘긴다
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] 선택 창을 띄우지 못해 예를 고른 것으로 진행합니다. ChoiceWidgetClass를 확인하세요."));
		HandleTutorialChoiceMade(true);
		return;
	}

	ChoiceWidgetInstance = CreateWidget<UCSTutorialChoiceWidget>(PlayerController, ChoiceClass);
	if (!IsValid(ChoiceWidgetInstance))
	{
		HandleTutorialChoiceMade(true);
		return;
	}

	ChoiceWidgetInstance->SetTexts(Row.PromptText, Row.ChoiceYesText, Row.ChoiceNoText);
	ChoiceWidgetInstance->OnChoiceMade.AddUniqueDynamic(this, &UCSTutorialSubsystem::HandleTutorialChoiceMade);
	ChoiceWidgetInstance->AddToViewport(TutorialWidgetZOrder + 1);
	PlayerController->SetGameplayUIInputLock(ChoiceWidgetInstance, true);

	// 같은 질문이 하단 대화창에도 떠서 겹친다
	ClearTutorialDisplay();
}

void UCSTutorialSubsystem::CloseChoiceWidget()
{
	if (!IsValid(ChoiceWidgetInstance))
	{
		ChoiceWidgetInstance = nullptr;
		return;
	}

	UCSTutorialChoiceWidget* Widget = ChoiceWidgetInstance;
	ChoiceWidgetInstance = nullptr;
	Widget->OnChoiceMade.RemoveDynamic(this, &UCSTutorialSubsystem::HandleTutorialChoiceMade);
	if (ACSStorePlayerController* PlayerController = Cast<ACSStorePlayerController>(Widget->GetOwningPlayer()))
	{
		PlayerController->SetGameplayUIInputLock(Widget, false);
	}
	Widget->RemoveFromParent();
}

void UCSTutorialSubsystem::HandleTutorialChoiceMade(bool bYes)
{
	if (bTutorialCompleted || !StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::CraftChoice)
	{
		return;
	}

	bCraftTutorialAccepted = bYes;
	bSkippingCraftBundle = !bYes;
	NotifyStepCompleted(ECSTutorialStepId::CraftChoice);
}

bool UCSTutorialSubsystem::CanPlayerSkipCurrentStep() const
{
	if (bTutorialCompleted || !StepRows.IsValidIndex(CurrentStepIndex))
	{
		return false;
	}
	if (bDeliveryTutorialActive)
	{
		return true;
	}

	// 제조는 선택 창의 아니요로 통째로 건너뛴다. 입간판과 잠자기는 넘겨도 그 행동은 해야 해서 안내만 사라진다.
	// 2부와 3부 시작 전에는 현재 스텝이 입간판에 머물러 있어서 여기서 같이 걸러진다
	const ECSTutorialStepId StepId = StepRows[CurrentStepIndex]->StepId;
	return StepId != ECSTutorialStepId::CraftMoveToHouse &&
		StepId != ECSTutorialStepId::CraftChoice &&
		!IsCraftBundleStep(StepId) &&
		StepId != ECSTutorialStepId::OpenSign &&
		StepId != ECSTutorialStepId::ApproachClosingBell;
}

void UCSTutorialSubsystem::HandleSkipKeyPressed()
{
	if (!CanPlayerSkipCurrentStep() || IsValid(ChoiceWidgetInstance) || IsValid(SkipConfirmWidgetInstance) ||
		!IsValid(SubscribedPlayerController))
	{
		return;
	}

	UClass* ChoiceClass = LoadClass<UCSTutorialChoiceWidget>(nullptr, SkipConfirmWidgetPath);
	SkipConfirmWidgetInstance = ChoiceClass != nullptr
		? CreateWidget<UCSTutorialChoiceWidget>(SubscribedPlayerController, ChoiceClass)
		: nullptr;
	if (!IsValid(SkipConfirmWidgetInstance))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] 건너뛰기 확인 창을 만들지 못했습니다. WBP_TutorialChoice 경로를 확인하세요."));
		return;
	}

	SkipConfirmStepIndex = CurrentStepIndex;
	bSkipConfirmForDelivery = bDeliveryTutorialActive;
	SkipConfirmDeliveryStepId = DeliveryStepId;
	SkipConfirmWidgetInstance->SetTexts(
		NSLOCTEXT("Tutorial", "SkipStepQuestion", "이 단계를 건너뛸까요?"),
		NSLOCTEXT("Tutorial", "SkipStepYes", "예"),
		NSLOCTEXT("Tutorial", "SkipStepNo", "아니요"));
	SkipConfirmWidgetInstance->OnChoiceMade.AddUniqueDynamic(this, &UCSTutorialSubsystem::HandleSkipConfirmChoice);
	SkipConfirmWidgetInstance->AddToViewport(TutorialWidgetZOrder + 1);
	SubscribedPlayerController->SetGameplayUIInputLock(SkipConfirmWidgetInstance, true);
}

void UCSTutorialSubsystem::CloseSkipConfirmWidget()
{
	SkipConfirmStepIndex = INDEX_NONE;
	bSkipConfirmForDelivery = false;
	if (!IsValid(SkipConfirmWidgetInstance))
	{
		SkipConfirmWidgetInstance = nullptr;
		return;
	}

	UCSTutorialChoiceWidget* Widget = SkipConfirmWidgetInstance;
	SkipConfirmWidgetInstance = nullptr;
	Widget->OnChoiceMade.RemoveDynamic(this, &UCSTutorialSubsystem::HandleSkipConfirmChoice);
	if (ACSStorePlayerController* PlayerController = Cast<ACSStorePlayerController>(Widget->GetOwningPlayer()))
	{
		PlayerController->SetGameplayUIInputLock(Widget, false);
	}
	Widget->RemoveFromParent();
}

void UCSTutorialSubsystem::HandleSkipConfirmChoice(bool bYes)
{
	const bool bSameStep = SkipConfirmStepIndex == CurrentStepIndex;
	const bool bForDelivery = bSkipConfirmForDelivery;
	const ECSTutorialStepId ConfirmedDeliveryStepId = SkipConfirmDeliveryStepId;
	CloseSkipConfirmWidget();
	if (!bYes)
	{
		return;
	}

	// 배달 중에는 일반 스텝이 입간판에 머물러 있어서 그쪽을 넘기면 안 된다
	if (bForDelivery)
	{
		if (bDeliveryTutorialActive && DeliveryStepId == ConfirmedDeliveryStepId)
		{
			SkipDeliveryStep();
		}
		return;
	}
	if (bSameStep && !bDeliveryTutorialActive && CanPlayerSkipCurrentStep())
	{
		DebugForceAdvanceStep();
	}
}

TArray<ACSProductPickupActor*> UCSTutorialSubsystem::PlaceTutorialIngredients()
{
	if (!IsValid(SubscribedCraftingSubsystem))
	{
		return {};
	}

	TArray<ACSProductPickupActor*> Ingredients = SubscribedCraftingSubsystem->PlaceProductsOnCraftCircle(TutorialIngredientIds);
	for (ACSProductPickupActor* Ingredient : Ingredients)
	{
		Ingredient->Tags.AddUnique(TutorialCraftItemTag);
		Ingredient->Tags.AddUnique(CSProductTags::TemporaryProduct);
	}
	return Ingredients;
}

void UCSTutorialSubsystem::AttachCraftMarker(AActor* Target, const FText& Label)
{
	if (!IsValid(Target) || !IsValid(Target->GetRootComponent()))
	{
		return;
	}

	// 동적으로 만든 마커에는 라벨 위젯이 없어서 레벨의 마커가 쓰는 위젯을 빌린다. 없으면 외곽선만 나온다
	TSubclassOf<UUserWidget> LabelWidgetClass;
	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(GetWorld()); It && LabelWidgetClass == nullptr; ++It)
		{
			if (const UCSTutorialMarkerComponent* Existing = It->FindComponentByClass<UCSTutorialMarkerComponent>())
			{
				LabelWidgetClass = Existing->GetWidgetClass();
			}
		}
	}

	UCSTutorialMarkerComponent* Marker = NewObject<UCSTutorialMarkerComponent>(Target);
	if (!IsValid(Marker))
	{
		return;
	}
	if (LabelWidgetClass != nullptr)
	{
		Marker->SetWidgetClass(LabelWidgetClass);
	}
	Marker->SetupAttachment(Target->GetRootComponent());
	Marker->RegisterComponent();
	Marker->ShowMarker(Label);
	CraftMarkerComponents.Add(Marker);
}

void UCSTutorialSubsystem::RemoveCraftMarkers()
{
	for (UCSTutorialMarkerComponent* Marker : CraftMarkerComponents)
	{
		if (IsValid(Marker))
		{
			Marker->HideMarker();
			Marker->DestroyComponent();
		}
	}

	CraftMarkerComponents.Reset();
}

void UCSTutorialSubsystem::BeginStoreCrateStep()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !StepRows.IsValidIndex(CurrentStepIndex))
	{
		return;
	}

	ACSDeliveryPortalActor* Portal = nullptr;
	for (TActorIterator<ACSDeliveryPortalActor> It(World); It && Portal == nullptr; ++It)
	{
		if (It->IsDeliveryConfigured())
		{
			Portal = *It;
		}
	}

	FCSOrderLine Line;
	Line.ProductRow.DataTable = LoadObject<UDataTable>(nullptr, StoreCrateProductTablePath);
	Line.ProductRow.RowName = StoreCrateProductId;
	Line.CrateCount = 1;
	TArray<ACSDeliveryCrateActor*> SpawnedCrates;
	if (IsValid(Portal) && IsValid(Line.ProductRow.DataTable) && Portal->SpawnOrderCrates({ Line }, SpawnedCrates) &&
		!SpawnedCrates.IsEmpty())
	{
		StoreCrateTarget = SpawnedCrates[0];
		AttachCraftMarker(SpawnedCrates[0], FText::GetEmpty());
		for (TActorIterator<ACSFreshnessSafeZoneActor> It(World); It; ++It)
		{
			if (It->GetStorageType() == ECSProductShelfType::ChilledShelf)
			{
				AttachCraftMarker(*It, StepRows[CurrentStepIndex]->MarkerLabelText);
			}
		}
	}
	else
	{
		// 상자가 없으면 아래 확인에서 바로 다음 스텝으로 넘어간다
		StoreCrateTarget = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] 창고 스텝 상자를 보내지 못해 이 스텝을 건너뜁니다."));
	}

	World->GetTimerManager().SetTimer(
		StoreCrateCheckTimerHandle, this, &UCSTutorialSubsystem::CheckStoreCrateStored, TutorialPollInterval, true);
}

void UCSTutorialSubsystem::CheckStoreCrateStored()
{
	// 창고 보관은 상자를 창고 액터에 붙이는 방식이라 부모 액터로 판정한다
	// 상자를 진열에 다 써서 비거나 사라진 경우도 넘겨야 튜토리얼이 막히지 않는다
	const ACSDeliveryCrateActor* Crate = StoreCrateTarget.Get();
	if (IsValid(Crate) && Crate->GetRemainingQuantity() > 0 &&
		Cast<ACSFreshnessSafeZoneActor>(Crate->GetAttachParentActor()) == nullptr)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StoreCrateCheckTimerHandle);
	}
	NotifyObjectiveProgress(ECSTutorialStepId::StoreCrate, 1);
}

void UCSTutorialSubsystem::CheckRestockTip()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (!bTipChainActive || ShownTipIds.Contains(ECSTutorialStepId::TipRestock))
	{
		World->GetTimerManager().ClearTimer(RestockTipCheckTimerHandle);
		return;
	}

	// 마감 뒤에는 다 팔린 게 정상이라 영업 중에만 본다
	const ACSStoreGameState* State = World->GetGameState<ACSStoreGameState>();
	const UCSCustomerRuntimeSubsystem* Runtime = World->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (!IsValid(State) || !State->bIsStoreOpen || !IsValid(Runtime))
	{
		return;
	}

	int32 ShelfCount = 0;
	int32 FilledSlots = 0;
	int32 TotalSlots = 0;
	const float FillRatio = Runtime->CalculateShelfFillRatio(ShelfCount, FilledSlots, TotalSlots);
	if (TotalSlots > 0 && FillRatio <= RestockTipFillRatio)
	{
		ShowOneShotTip(ECSTutorialStepId::TipRestock);
	}
}

void UCSTutorialSubsystem::HandleCraftIngredientAdded(int32 IngredientCount)
{
	// 1부 상자처럼 집을 때 끄는 신호가 없어서 솥에 들어가 숨겨진 재료의 마커를 끈다
	for (UCSTutorialMarkerComponent* Marker : CraftMarkerComponents)
	{
		if (IsValid(Marker) && IsValid(Marker->GetOwner()) && Marker->GetOwner()->IsHidden())
		{
			Marker->HideMarker();
		}
	}

	if (!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::CraftAddIngredients)
	{
		return;
	}

	NotifyObjectiveProgress(ECSTutorialStepId::CraftAddIngredients, IngredientCount);
}

void UCSTutorialSubsystem::HandleManualCraftFinished(ECSCraftOutcome Outcome)
{
	// 안내 문구는 솥 상태를 조금 늦게 따라가서 거품 스텝이 뜨기 전에 끝날 수도 있다
	if (bTutorialCompleted || bCraftBrewSucceeded || !StepRows.IsValidIndex(CurrentStepIndex) ||
		!IsCraftBrewPhaseStep(StepRows[CurrentStepIndex]->StepId) ||
		!IsValid(SubscribedCraftingSubsystem))
	{
		return;
	}

	const bool bSucceeded = Outcome == ECSCraftOutcome::Success || Outcome == ECSCraftOutcome::GreatSuccess;
	if (bSucceeded || Outcome == ECSCraftOutcome::Junk)
	{
		SubscribedCraftingSubsystem->AddTagsToCraftResults({ TutorialCraftItemTag, CSProductTags::TemporaryProduct });
	}

	if (bSucceeded)
	{
		bCraftBrewSucceeded = true;
		CheckCraftTutorialState();
		return;
	}

	PlaceTutorialIngredients();
	// 지금 띄우면 제조대가 바로 뒤에 띄우는 결과 문구에 덮인다
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UCSTutorialSubsystem> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick([WeakThis]()
		{
			if (WeakThis.IsValid() && IsValid(WeakThis->SubscribedCraftingSubsystem))
			{
				WeakThis->SubscribedCraftingSubsystem->ShowCraftStationMessage(TutorialRetryMessageRow, false);
			}
		});
	}
}

void UCSTutorialSubsystem::CleanUpCraftTutorial()
{
	CloseChoiceWidget();

	bCraftIngredientsPlaced = false;
	bCraftBrewSucceeded = false;

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(CraftStateCheckTimerHandle);

	if (UCSCraftingSubsystem* CraftingSubsystem = World->GetSubsystem<UCSCraftingSubsystem>())
	{
		CraftingSubsystem->CraftInputGate.Unbind();
		CraftingSubsystem->SetCraftCloseupGuidesHidden(false);
		CraftingSubsystem->CloseCraftStationPanels();
		// 아니요를 골랐으면 솥에 튜토리얼 물건이 없고, 플레이어가 넣어 둔 재료만 있을 수 있다
		if (bCraftTutorialAccepted)
		{
			CraftingSubsystem->ClearCraftStationPot();
		}
	}
	bCraftTutorialAccepted = false;

	TArray<ACSProductPickupActor*> TutorialItems;
	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		if (IsValid(*It) && It->ActorHasTag(TutorialCraftItemTag))
		{
			TutorialItems.Add(*It);
		}
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	ACSPlayerCharacter* PlayerCharacter = PlayerController != nullptr ? Cast<ACSPlayerCharacter>(PlayerController->GetPawn()) : nullptr;
	for (ACSProductPickupActor* Item : TutorialItems)
	{
		// 손이나 퀵바에 남은 참조부터 떼어야 지운 뒤 퀵바 아이콘이 남지 않는다
		if (IsValid(PlayerCharacter))
		{
			PlayerCharacter->RemoveProductFromHands(Item);
		}
		ACSDeliveryCrateActor::RemovePackedSource(Item);
	}

	RemoveCraftMarkers();
}

void UCSTutorialSubsystem::SubscribeToDeliverySubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UCSDeliveryOrderSubsystem* DeliverySubsystem = World->GetSubsystem<UCSDeliveryOrderSubsystem>())
		{
			SubscribedDeliverySubsystem = DeliverySubsystem;
			DeliverySubsystem->OnDeliveryOrderChanged.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleDeliveryOrderChanged);
		}
	}
}

void UCSTutorialSubsystem::UnsubscribeFromDeliverySubsystem()
{
	if (IsValid(SubscribedDeliverySubsystem))
	{
		SubscribedDeliverySubsystem->OnDeliveryOrderChanged.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleDeliveryOrderChanged);
	}

	SubscribedDeliverySubsystem = nullptr;
}

void UCSTutorialSubsystem::HandleDeliveryOrderChanged(const FCSDeliveryOrderRuntime& Order)
{
	if (!bDeliveryTutorialActive)
	{
		// 개점 뒤 첫 주문이 연습용 주문이다
		if (bDeliveryTutorialDone || bTutorialCompleted || !bTipChainActive ||
			Order.Status != ECSDeliveryOrderStatus::PendingAcceptance || Order.RequiredProducts.IsEmpty())
		{
			return;
		}

		bDeliveryTutorialActive = true;
		DeliveryTutorialOrderId = Order.OrderId;
		const FDataTableRowHandle& ProductRow = Order.RequiredProducts[0].ProductRow;
		const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSTutorialSubsystem::HandleDeliveryOrderChanged"));
		DeliveryProductId = ProductRow.RowName;
		DeliveryProductName = ProductData != nullptr ? ProductData->DisplayName : FText::FromName(DeliveryProductId);
		EnterDeliveryStep(ECSTutorialStepId::DeliveryAccept);
		return;
	}

	if (Order.OrderId != DeliveryTutorialOrderId)
	{
		return;
	}

	switch (Order.Status)
	{
	case ECSDeliveryOrderStatus::PendingAcceptance:
		break;
	case ECSDeliveryOrderStatus::Accepted:
		if (DeliveryStepId == ECSTutorialStepId::DeliveryAccept)
		{
			EnterDeliveryStep(ECSTutorialStepId::DeliveryPickUp);
		}
		break;
	case ECSDeliveryOrderStatus::Completed:
		FinishDeliveryTutorial(false);
		break;
	default:
		// 만료, 실패, 거절, 취소. 1일차는 주문 한도가 1건이라 다시 시키지 않는다
		FinishDeliveryTutorial(true);
		break;
	}
}

void UCSTutorialSubsystem::EnterDeliveryStep(ECSTutorialStepId StepId)
{
	DeliveryStepId = StepId;
	RemoveCraftMarkers();
	HideAllActiveMarkers();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeliveryCheckTimerHandle);
	}

	const FCSTutorialStepRow* Row = FindTipRow(StepId);
	if (Row == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Tutorial] DT_TutorialStep에 배달 튜토리얼 행이 없습니다. 안내 없이 주문 상태만 따라갑니다."));
		return;
	}

	// 행 문구의 {Product} 자리에 주문 상품 이름을 넣는다
	FFormatNamedArguments Args;
	Args.Add(TEXT("Product"), DeliveryProductName);
	DeliveryShownRow = *Row;
	DeliveryShownRow.PromptText = FText::Format(Row->PromptText, Args);
	DeliveryShownRow.ObjectiveText = FText::Format(Row->ObjectiveText, Args);
	BroadcastStepRow(DeliveryShownRow);
	OnTutorialObjectiveProgress.Broadcast(0, DeliveryShownRow.ObjectiveGoal);
	// 팁과 달리 끝날 때까지 남는 안내라 정산창을 닫으면 되살린다
	LastBroadcastRow = &DeliveryShownRow;
	LastBroadcastObjectiveCurrent = 0;

	switch (StepId)
	{
	case ECSTutorialStepId::DeliveryAccept:
		ShowMarkersForTag(Row->HighlightMarkerTag, Row->MarkerLabelText);
		break;
	case ECSTutorialStepId::DeliveryPickUp:
		bDeliveryProductSoldOut = false;
		AttachDeliveryProductMarkers();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				DeliveryCheckTimerHandle, this, &UCSTutorialSubsystem::CheckDeliveryPickUp, CraftStatePollInterval, true);
		}
		break;
	case ECSTutorialStepId::DeliveryDispatch:
		AttachCraftMarker(FindDeliveryPlaceActor(), Row->MarkerLabelText);
		break;
	default:
		break;
	}
}

void UCSTutorialSubsystem::CheckDeliveryPickUp()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (!bDeliveryTutorialActive || DeliveryStepId != ECSTutorialStepId::DeliveryPickUp)
	{
		World->GetTimerManager().ClearTimer(DeliveryCheckTimerHandle);
		return;
	}

	// 낱개로 들어도, 그 상품 상자를 들어도 마법진에 올릴 수 있다
	const APlayerController* PlayerController = World->GetFirstPlayerController();
	const ACSPlayerCharacter* PlayerCharacter = PlayerController != nullptr ? Cast<ACSPlayerCharacter>(PlayerController->GetPawn()) : nullptr;
	const AActor* HeldActor = IsValid(PlayerCharacter) ? PlayerCharacter->GetHeldActor() : nullptr;
	FName HeldProductId;
	if (const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor))
	{
		HeldProductId = HeldProduct->GetProductId();
	}
	else if (const ACSDeliveryCrateActor* HeldCrate = Cast<ACSDeliveryCrateActor>(HeldActor); IsValid(HeldCrate) && HeldCrate->GetRemainingQuantity() > 0)
	{
		HeldProductId = HeldCrate->GetProductId();
	}
	if (!HeldProductId.IsNone() && HeldProductId == DeliveryProductId)
	{
		EnterDeliveryStep(ECSTutorialStepId::DeliveryDispatch);
		return;
	}

	// 예약은 손님 구매를 막지 않아서 수락한 뒤에 다 팔릴 수 있다. 한 번 알리고, 다시 생기면 마커를 새로 붙인다
	const UCSStoreStockSubsystem* StockSubsystem = World->GetSubsystem<UCSStoreStockSubsystem>();
	const bool bSoldOut = IsValid(StockSubsystem) && StockSubsystem->GetProductStock(DeliveryProductId).TotalQuantity <= 0;
	if (bSoldOut && !bDeliveryProductSoldOut)
	{
		bDeliveryProductSoldOut = true;
		RemoveCraftMarkers();
		if (IsValid(BannerWidgetInstance))
		{
			BannerWidgetInstance->ShowTipBanner(NSLOCTEXT("Tutorial", "DeliveryProductSoldOut",
				"주문 상품이 다 팔렸어요.\n창고나 즉시 배송으로 채운 뒤 집어 보세요."));
		}
	}
	else if (!bSoldOut && bDeliveryProductSoldOut)
	{
		bDeliveryProductSoldOut = false;
		AttachDeliveryProductMarkers();
	}
}

void UCSTutorialSubsystem::AttachDeliveryProductMarkers()
{
	RemoveCraftMarkers();
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	bool bMarkedShelfProduct = false;
	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsHidden() && It->GetProductId() == DeliveryProductId &&
			Cast<ACSShelfActor>(It->GetAttachParentActor()) != nullptr)
		{
			AttachCraftMarker(*It, FText::GetEmpty());
			bMarkedShelfProduct = true;
		}
	}
	if (bMarkedShelfProduct)
	{
		return;
	}

	// 진열대에 없으면 창고나 바닥에 있는 그 상품 상자를 알려 준다
	for (TActorIterator<ACSDeliveryCrateActor> It(World); It; ++It)
	{
		if (IsValid(*It) && It->GetProductId() == DeliveryProductId && It->GetRemainingQuantity() > 0)
		{
			AttachCraftMarker(*It, FText::GetEmpty());
		}
	}
}

AActor* UCSTutorialSubsystem::FindDeliveryPlaceActor() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	ACSDeliveryMagicCircleActor* MagicCircle = nullptr;
	for (TActorIterator<ACSDeliveryMagicCircleActor> It(World); It && MagicCircle == nullptr; ++It)
	{
		MagicCircle = *It;
	}
	if (!IsValid(MagicCircle))
	{
		return nullptr;
	}

	AActor* ClosestPlace = nullptr;
	double ClosestDistanceSquared = FMath::Square(DeliveryPlaceMaxDistance);
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const UStaticMeshComponent* MeshComponent = It->GetStaticMeshComponent();
		const UStaticMesh* Mesh = IsValid(MeshComponent) ? MeshComponent->GetStaticMesh() : nullptr;
		if (!IsValid(Mesh) || Mesh->GetFName() != DeliveryPlaceMeshName)
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared2D(It->GetActorLocation(), MagicCircle->GetActorLocation());
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestPlace = *It;
		}
	}

	// 바닥 타일을 못 찾으면 마법진 자체를 표시한다
	return ClosestPlace != nullptr ? ClosestPlace : MagicCircle;
}

void UCSTutorialSubsystem::SkipDeliveryStep()
{
	switch (DeliveryStepId)
	{
	case ECSTutorialStepId::DeliveryAccept:
	{
		// 수락되면 주문 상태 방송으로 집기 단계가 시작된다
		FText Message;
		if ((!IsValid(SubscribedDeliverySubsystem) || !SubscribedDeliverySubsystem->AcceptOrder(DeliveryTutorialOrderId, Message)) &&
			IsValid(BannerWidgetInstance) && !Message.IsEmpty())
		{
			BannerWidgetInstance->ShowTipBanner(Message);
		}
		break;
	}
	case ECSTutorialStepId::DeliveryPickUp:
		EnterDeliveryStep(ECSTutorialStepId::DeliveryDispatch);
		break;
	default:
		FinishDeliveryTutorial(false);
		break;
	}
}

void UCSTutorialSubsystem::FinishDeliveryTutorial(bool bOrderEndedEarly)
{
	// 남은 주문은 거절·취소·기한이 막힌 연습 주문이라 일반 주문으로 되돌려야 처리할 수 있다
	if (IsValid(SubscribedDeliverySubsystem))
	{
		SubscribedDeliverySubsystem->ReleaseGuidedOrder();
	}

	bDeliveryTutorialActive = false;
	bDeliveryTutorialDone = true;
	DeliveryTutorialOrderId = INDEX_NONE;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeliveryCheckTimerHandle);
	}
	RemoveCraftMarkers();
	ClearTutorialDisplay();

	// 마감 뒤에는 2부 팁을 3부에 끌고 가지 않는다
	if (!bTipChainActive)
	{
		bHasPendingChainTip = false;
		DeferredBannerTipIds.Reset();
		return;
	}

	if (bOrderEndedEarly && IsValid(BannerWidgetInstance))
	{
		BannerWidgetInstance->ShowTipBanner(NSLOCTEXT("Tutorial", "DeliveryTutorialOrderEnded",
			"배달 주문이 끝났어요. 다음 주문 때 다시 해 봐요."));
	}

	// 배달 전에 떠 있었거나 배달 중에 차례가 된 대화창 팁을 다시 띄운다
	if (bHasPendingChainTip)
	{
		if (const FCSTutorialStepRow* ChainTipRow = FindTipRow(PendingChainTipId))
		{
			DisplayTip(*ChainTipRow);
		}
	}
	for (const ECSTutorialStepId TipId : DeferredBannerTipIds)
	{
		if (const FCSTutorialStepRow* BannerTipRow = FindTipRow(TipId))
		{
			DisplayTip(*BannerTipRow);
		}
	}
	DeferredBannerTipIds.Reset();
}
