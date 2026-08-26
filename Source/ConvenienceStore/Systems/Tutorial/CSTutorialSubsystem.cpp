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
#include "Systems/Pricing/CSStorePricingSubsystem.h"
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
	UnsubscribeFromLevelShelves();
	UnsubscribeFromPlayerCharacter();
	UnsubscribeFromPlayerController();
	UnsubscribeFromOrderSubsystem();
	UnsubscribeFromPricingSubsystem();
	UnsubscribeFromStoreGameState();
	CloseTutorialWidgets();

	Super::Deinitialize();
}

void UCSTutorialSubsystem::StartTutorial(ACSStoreGameMode* GameMode)
{
	StepRows.Reset();
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
	SubscribeToLevelShelves();
	TrySubscribeToPlayerController();
	SubscribeToOrderSubsystem();
	SubscribeToPricingSubsystem();
	SubscribeToStoreGameState();

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

	if (!StepRows.IsValidIndex(NextIndex))
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
	UnsubscribeFromLevelShelves();
	UnsubscribeFromPlayerCharacter();
	UnsubscribeFromPlayerController();
	UnsubscribeFromOrderSubsystem();
	UnsubscribeFromPricingSubsystem();
	UnsubscribeFromStoreGameState();
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
			MarkerComponent->ShowMarker(CurrentRow.MarkerLabelText);
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

	AdvanceStep();
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

void UCSTutorialSubsystem::SubscribeToPricingSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UCSStorePricingSubsystem* PricingSubsystem = World->GetSubsystem<UCSStorePricingSubsystem>())
		{
			SubscribedPricingSubsystem = PricingSubsystem;
			PricingSubsystem->OnSalePriceChanged.AddUniqueDynamic(
				this, &UCSTutorialSubsystem::HandleSalePriceChanged);
		}
	}
}

void UCSTutorialSubsystem::UnsubscribeFromPricingSubsystem()
{
	if (IsValid(SubscribedPricingSubsystem))
	{
		SubscribedPricingSubsystem->OnSalePriceChanged.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleSalePriceChanged);
	}

	SubscribedPricingSubsystem = nullptr;
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
		}
	}
}

void UCSTutorialSubsystem::UnsubscribeFromStoreGameState()
{
	if (IsValid(SubscribedStoreGameState))
	{
		SubscribedStoreGameState->OnStoreOpenStateChanged.RemoveDynamic(
			this, &UCSTutorialSubsystem::HandleStoreOpenStateChanged);
	}

	SubscribedStoreGameState = nullptr;
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

	if (!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::PlaceProductOrder)
	{
		return;
	}

	NotifyStepCompleted(ECSTutorialStepId::PlaceProductOrder);
}

void UCSTutorialSubsystem::HandleSalePriceChanged(FName ProductRowName, int32 NewSalePrice)
{
	(void)ProductRowName;
	(void)NewSalePrice;   // 이 델리게이트는 변경 성공 시에만 방송되므로 값 자체는 볼 필요 없음

	if (!StepRows.IsValidIndex(CurrentStepIndex) ||
		StepRows[CurrentStepIndex]->StepId != ECSTutorialStepId::SetProductPrice)
	{
		return;
	}

	NotifyStepCompleted(ECSTutorialStepId::SetProductPrice);
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
