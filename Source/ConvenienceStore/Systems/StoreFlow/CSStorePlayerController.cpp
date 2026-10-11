#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Journal/CSJournalWidget.h"
#include "Features/Journal/CSJournalWorldActors.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Components/InputComponent.h"
#include "Engine/GameInstance.h"

#include "Blueprint/UserWidget.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/HUD/CSStoreHUDWidget.h"
#include "UI/HUD/CSStoreTopBarWidget.h"
#include "UI/Store/CSDailySummaryWidget.h"
#include "UI/Store/CSStatusAnnouncementWidget.h"
#include "UI/Store/CSClosingWarningWidget.h"
#include "Features/UI/CSDeliveryToastWidget.h"
#include "Systems/SaveLoad/CSSaveSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "UI/Tutorial/CSNoticeBannerWidgetBase.h"
#include "UI/Settings/CSSettingsWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Features/Store/CSStoreOpeningDirector.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SGameLayerManager.h"
#include "Systems/Tutorial/CSUnlockGuideSubsystem.h"

// 게임 입력 상태에서는 Tab/C/Esc로 각 화면을 열고 열린 화면에서는 위젯이 닫기 입력을 처리합니다.
void ACSStorePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// 레벨업은 자동 연출로 진행하며, 다시 읽기 단축키만 유지합니다.
	if (UCSUnlockGuideSubsystem* Guide = GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>())
	{
		InputComponent->BindKey(UnlockGuideHistoryKey, IE_Pressed, Guide, &UCSUnlockGuideSubsystem::ShowGuideHistory);
	}
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ACSStorePlayerController::ToggleJournal);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &ACSStorePlayerController::ToggleDailySummary);

	// 입력 에셋이 지정되지 않았으면 기존 Esc 바인딩을 그대로 쓴다
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (IsValid(EnhancedInput) && IsValid(ToggleSettingsAction))
	{
		EnhancedInput->BindAction(ToggleSettingsAction, ETriggerEvent::Started, this, &ACSStorePlayerController::ToggleSettings);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] ToggleSettingsAction이 지정되지 않아 Esc를 옛 방식으로 받습니다. 솥 클로즈업 중 Esc가 설정창과 겹칠 수 있습니다."));
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACSStorePlayerController::ToggleSettings);
	}
}
// 다른 조작 화면이나 일시 정지 상태와 충돌하지 않도록 확인한 뒤 수첩을 엽니다.
void ACSStorePlayerController::ToggleJournal()
{
	if (bCraftCloseupActive) return;
	if (IsValid(ActiveJournalWidget)) { CloseJournal(); return; }
	if (!IsLocalPlayerController() || IsGameplayUIInputLocked() || IsPaused()) return;
	// WBP가 준비되지 않았으면 입력과 HUD 상태를 변경하지 않습니다.
	UClass* WidgetClass = JournalWidgetClass.Get();
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Journal] JournalWidgetClass에 CSJournalWidget 기반 WBP를 지정해 주세요."));
		return;
	}
	ActiveJournalWidget = CreateWidget<UCSJournalWidget>(this, WidgetClass);
	if (!ActiveJournalWidget) return;
	ActiveJournalWidget->AddToViewport(50);
	SetStoreHUDHidden(ActiveJournalWidget, true);
	SetGameplayUIInputLock(ActiveJournalWidget, true);
	ActiveJournalWidget->SetKeyboardFocus();
}
// 수첩이 요청한 입력 잠금과 HUD 숨김만 해제하여 다른 UI의 상태를 유지합니다.
void ACSStorePlayerController::CloseJournal()
{
	if (!IsValid(ActiveJournalWidget)) return;
	SetGameplayUIInputLock(ActiveJournalWidget, false);
	SetStoreHUDHidden(ActiveJournalWidget, false);
	ActiveJournalWidget->RemoveFromParent();
	ActiveJournalWidget = nullptr;
}

// 다른 UI가 열려 있으면 Esc가 그 UI를 먼저 닫음(UIOnly 입력 모드) — 여기 닫기 검사는 사실상 X 버튼 경로만 남음.
void ACSStorePlayerController::ToggleSettings()
{
	if (IsValid(ActiveSettingsWidget)) { CloseSettings(); return; }
	if (!IsLocalPlayerController() || IsGameplayUIInputLocked() || IsPaused()) return;
	UClass* WidgetClass = SettingsWidgetClass.Get();
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] SettingsWidgetClass에 CSSettingsWidget 기반 WBP를 지정해 주세요."));
		return;
	}
	ActiveSettingsWidget = CreateWidget<UCSSettingsWidget>(this, WidgetClass);
	if (!ActiveSettingsWidget) return;
	// 튜토리얼 배너/대화창/목표(TutorialWidgetZOrder=1150)보다 위에 그려야 설정창이 열려 있는 동안
	// 튜토리얼 UI에 안 가려짐.
	ActiveSettingsWidget->AddToViewport(1500);
	SetStoreHUDHidden(ActiveSettingsWidget, true);
	SetGameplayUIInputLock(ActiveSettingsWidget, true);
	ActiveSettingsWidget->SetKeyboardFocus();
}

void ACSStorePlayerController::CloseSettings()
{
	if (!IsValid(ActiveSettingsWidget)) return;
	SetGameplayUIInputLock(ActiveSettingsWidget, false);
	SetStoreHUDHidden(ActiveSettingsWidget, false);
	ActiveSettingsWidget->RemoveFromParent();
	ActiveSettingsWidget = nullptr;
}

void ACSStorePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalPlayerController()) CSConfigureJournalWorld(GetWorld());
	if (IsLocalPlayerController()) UCSSettingsWidget::ApplyStartupGraphicsDefault();

	// 솥 클로즈업 IMC가 이보다 우선순위가 높아서 Esc를 먼저 가져간다
	if (IsLocalPlayerController() && IsValid(ControllerMappingContext))
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			InputSubsystem->AddMappingContext(ControllerMappingContext, 0);
		}
	}

	if (IsLocalPlayerController() && StoreHUDWidgetClass != nullptr)
	{
		StoreHUDWidget = CreateWidget<UCSStoreHUDWidget>(this, StoreHUDWidgetClass);
		if (StoreHUDWidget != nullptr)
		{
			StoreHUDWidget->AddToPlayerScreen();
			// 최초 생성 시에는 영업 모드 아이콘만 맞추고 전환 연출은 재생하지 않습니다.
			StoreHUDWidget->SetBuildModeActive(false, false);
			if (CachedQuickbarSlotCount > 0)
			{
				TArray<UTexture2D*> QuickbarIcons;
				QuickbarIcons.Reserve(CachedQuickbarToolIcons.Num());
				for (UTexture2D* ToolIcon : CachedQuickbarToolIcons)
				{
					QuickbarIcons.Add(ToolIcon);
				}
				StoreHUDWidget->SetQuickbarData(
					CachedQuickbarSlotCount,
					CachedQuickbarSelectedSlotIndex,
					CachedQuickbarToolNames,
					QuickbarIcons);
			}
		}
	}
	if (IsLocalPlayerController() && StoreTopBarWidgetClass != nullptr)
	{
		StoreTopBarWidget = CreateWidget<UCSStoreTopBarWidget>(this, StoreTopBarWidgetClass);
		if (IsValid(StoreTopBarWidget))
		{
			// 발주창 등 Viewport UI보다 위에 두고, 타이틀과 엔딩에서는 별도로 숨깁니다.
			StoreTopBarWidget->AddToViewport(2000);
			StoreTopBarWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
			StoreTopBarWidget->SetBuildModeActive(false, false);
		}
	}

	// Temporary Title Screen: 이 한 줄을 제거하면 기존 시작 흐름으로 즉시 복귀합니다.
	ShowTemporaryTitleScreen();

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreGameState != nullptr)
	{
		LastObservedStorePhase = StoreGameState->CurrentPhase;
		StoreGameState->OnEndOfDayTaskChanged.AddDynamic(
			this, &ACSStorePlayerController::HandleEndOfDayTaskChanged);
		StoreGameState->OnStorePhaseChanged.AddDynamic(
			this, &ACSStorePlayerController::HandleStorePhaseChanged);
		StoreGameState->OnDailySummaryRequested.AddDynamic(this, &ACSStorePlayerController::HandleDailySummaryRequested);
		StoreGameState->OnClosingWarningRequested.AddUniqueDynamic(
			this, &ACSStorePlayerController::HandleClosingWarningRequested);
		UpdateClosingInputMode();
	}

	// 타이틀 없이 에디터에서 실행하셔도 새 게임 전경을 한 번 재생합니다.
	// 불러온 게임은 기존 위치와 시점을 유지하도록 연출을 생략합니다.
	const UCSSaveSubsystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UCSSaveSubsystem>() : nullptr;
	if (IsLocalPlayerController() && !IsValid(ActiveTemporaryTitleWidget) &&
		(!SaveSubsystem || !SaveSubsystem->HasPendingLoad()))
	{
		bOpeningPresentationRequested = true;
		OpeningStartTimer = GetWorldTimerManager().SetTimerForNextTick(
			this, &ThisClass::StartOpeningPresentation);
	}
}

void ACSStorePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (HasActorBegunPlay() && bOpeningPresentationRequested && !bOpeningPresentationPlayed)
	{
		// 플레이어 생성이 늦어져도 기본 카메라 설정이 끝난 다음 프레임에 연출을 시작합니다.
		GetWorldTimerManager().ClearTimer(OpeningStartTimer);
		OpeningStartTimer = GetWorldTimerManager().SetTimerForNextTick(
			this, &ThisClass::StartOpeningPresentation);
	}
}

// === Temporary Title Screen: 정식 타이틀 도입 시 아래 세 함수 전체 제거 ===
void ACSStorePlayerController::ShowTemporaryTitleScreen()
{
	if (!bUseTemporaryTitleScreen || !IsLocalPlayerController() ||
		!TemporaryTitleWidgetClass || IsValid(ActiveTemporaryTitleWidget))
	{
		return;
	}

	// 불러오기로 다시 열린 월드는 타이틀 일시 정지로 복원 타이머가 멈추지 않도록 타이틀을 생략
	if (const UGameInstance* OwningGameInstance = GetGameInstance())
	{
		const UCSSaveSubsystem* SaveSubsystem = OwningGameInstance->GetSubsystem<UCSSaveSubsystem>();
		if (SaveSubsystem != nullptr && SaveSubsystem->HasPendingLoad())
		{
			return;
		}
	}

	UClass* LoadedTitleWidgetClass = TemporaryTitleWidgetClass.Get();
	if (LoadedTitleWidgetClass == nullptr)
	{
		return;
	}

	ActiveTemporaryTitleWidget = CreateWidget<UUserWidget>(this, LoadedTitleWidgetClass);
	if (!IsValid(ActiveTemporaryTitleWidget))
	{
		return;
	}

	ActiveTemporaryTitleWidget->AddToViewport(1001);
	SetStoreHUDHidden(ActiveTemporaryTitleWidget, true);
	SetStoreTopBarHidden(ActiveTemporaryTitleWidget, true);
	SetGameplayUIInputLock(ActiveTemporaryTitleWidget, true);
	SetPause(true);
}

void ACSStorePlayerController::StartGameFromTemporaryTitle()
{
	if (bOpeningPresentationPlayed || bOpeningPresentationActive) return;
	// 이 버튼은 최초 생성된 월드를 진행하므로 첫날 기록을 유지합니다. 별도의 새 게임 시작 시에는 ResetJournal을 호출해 주시면 됩니다.
	bOpeningPresentationRequested = true;
	CloseTemporaryTitleScreen();
	// 타이틀의 일시정지를 해제한 뒤 오프닝이 자신의 정지 상태를 관리하도록 합니다.
	StartOpeningPresentation();
}

void ACSStorePlayerController::StartOpeningPresentation()
{
	if (!bUseOpeningPresentation || bOpeningPresentationPlayed ||
		!IsLocalPlayerController() || !IsValid(GetPawn())) return;
	// 가게 앞 PlayerStart의 시점을 유지하며 눈뜨기와 대화만 진행합니다.
	if (UCSUnlockGuideSubsystem* Guide = GetWorld()->GetSubsystem<UCSUnlockGuideSubsystem>())
		bOpeningPresentationPlayed = Guide->StartOpeningStory();
}

void ACSStorePlayerController::FinishOpeningPresentation()
{
	if (!bOpeningPresentationActive) return;
	if (IsValid(ActiveOpeningDirector))
	{
		ActiveOpeningDirector->OnOpeningFinished.RemoveDynamic(this, &ThisClass::FinishOpeningPresentation);
	}
	ActiveOpeningDirector = nullptr;
	bOpeningPresentationActive = false;
	SetStoreHUDHidden(this, false);
	SetStoreTopBarHidden(this, false);
	SetOpeningUIHidden(false);
	UpdateClosingInputMode();
	if (!IsActorBeingDestroyed())
	{
		TryShowQueuedDeliveryToast();
		TryShowPendingClosingAnnouncement();
	}
}

void ACSStorePlayerController::SetOpeningUIHidden(bool bShouldHide)
{
	if (bShouldHide)
	{
		UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
		if (!ViewportClient) return;
		const TSharedPtr<IGameLayerManager> LayerManager = ViewportClient->GetGameLayerManager();
		if (!LayerManager.IsValid()) return;
		const TSharedRef<SWidget> UILayer = LayerManager->AsWidget();
		OpeningUILayer = UILayer;
		UILayerVisibilityBeforeOpening = UILayer->GetVisibility();
		// UI 전체를 숨겨 오프닝 중 새로 생성되는 튜토리얼과 알림도 표시되지 않게 합니다.
		UILayer->SetVisibility(EVisibility::Hidden);
	}
	else
	{
		// 오프닝과 카메라 복귀가 끝나면 각 위젯의 상태를 유지한 채 UI 표시를 복원합니다.
		if (const TSharedPtr<SWidget> UILayer = OpeningUILayer.Pin())
		{
			UILayer->SetVisibility(UILayerVisibilityBeforeOpening);
		}
		OpeningUILayer.Reset();
	}
}

void ACSStorePlayerController::CloseTemporaryTitleScreen()
{
	if (!IsValid(ActiveTemporaryTitleWidget))
	{
		return;
	}

	UUserWidget* TitleWidgetToClose = ActiveTemporaryTitleWidget;
	SetPause(false);
	SetGameplayUIInputLock(TitleWidgetToClose, false);
	SetStoreHUDHidden(TitleWidgetToClose, false);
	SetStoreTopBarHidden(TitleWidgetToClose, false);
	TitleWidgetToClose->RemoveFromParent();
	ActiveTemporaryTitleWidget = nullptr;
}
// === End Temporary Title Screen ===

void ACSStorePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(OpeningStartTimer);
	FinishOpeningPresentation();
	CloseJournal();
	CloseSettings();
	CloseTemporaryTitleScreen();

	if (StoreGameState != nullptr)
	{
		StoreGameState->OnEndOfDayTaskChanged.RemoveDynamic(
			this, &ACSStorePlayerController::HandleEndOfDayTaskChanged);
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &ACSStorePlayerController::HandleStorePhaseChanged);
		StoreGameState->OnDailySummaryRequested.RemoveDynamic(this, &ACSStorePlayerController::HandleDailySummaryRequested);
		StoreGameState->OnClosingWarningRequested.RemoveDynamic(
			this, &ACSStorePlayerController::HandleClosingWarningRequested);
	}

	if (IsValid(ActiveStatusAnnouncementWidget))
	{
		ActiveStatusAnnouncementWidget->OnAnnouncementFinished.RemoveDynamic(
			this, &ACSStorePlayerController::HandleStatusAnnouncementFinished);
		ActiveStatusAnnouncementWidget->RemoveFromParent();
		ActiveStatusAnnouncementWidget = nullptr;
	}

	if (IsValid(ActiveClosingWarningWidget))
	{
		ActiveClosingWarningWidget->OnWarningFinished.RemoveDynamic(
			this, &ACSStorePlayerController::HandleClosingWarningFinished);
		ActiveClosingWarningWidget->RemoveFromParent();
		ActiveClosingWarningWidget = nullptr;
	}
	if (IsValid(ActiveDeliveryToastWidget))
	{
		ActiveDeliveryToastWidget->OnToastFinished.RemoveDynamic(
			this, &ThisClass::HandleDeliveryToastFinished);
		ActiveDeliveryToastWidget->RemoveFromParent();
		ActiveDeliveryToastWidget = nullptr;
	}
	if (IsValid(ActiveNoticeBannerWidget))
	{
		ActiveNoticeBannerWidget->RemoveFromParent();
		ActiveNoticeBannerWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ACSStorePlayerController::ShowStatusAnnouncement(
	ECSStatusAnnouncementType AnnouncementType)
{
	if (!IsLocalPlayerController() || !IsValid(StoreGameState) ||
		StatusAnnouncementWidgetClass == nullptr)
	{
		return;
	}

	if (!IsValid(ActiveStatusAnnouncementWidget))
	{
		ActiveStatusAnnouncementWidget = CreateWidget<UCSStatusAnnouncementWidget>(
			this,
			StatusAnnouncementWidgetClass);
		if (!IsValid(ActiveStatusAnnouncementWidget))
		{
			return;
		}

		ActiveStatusAnnouncementWidget->OnAnnouncementFinished.AddUniqueDynamic(
			this, &ACSStorePlayerController::HandleStatusAnnouncementFinished);
		ActiveStatusAnnouncementWidget->AddToPlayerScreen(100);
	}

	ActiveStatusAnnouncementWidget->PlayAnnouncement(AnnouncementType);
}

void ACSStorePlayerController::HandleStatusAnnouncementFinished()
{
	if (!IsValid(ActiveStatusAnnouncementWidget))
	{
		return;
	}

	if (ActiveStatusAnnouncementWidget->GetAnnouncementType() == ECSStatusAnnouncementType::Opened)
	{
		OnStoreOpenedAnnouncementFinished.Broadcast();
	}
	else
	{
		OnStoreClosedAnnouncementFinished.Broadcast();
	}

	ActiveStatusAnnouncementWidget->OnAnnouncementFinished.RemoveDynamic(
		this, &ACSStorePlayerController::HandleStatusAnnouncementFinished);
	ActiveStatusAnnouncementWidget->RemoveFromParent();
	ActiveStatusAnnouncementWidget = nullptr;
}

void ACSStorePlayerController::ShowGameplayToast(const FText& Title, const FText& Message)
{
	if (!IsLocalPlayerController() || DeliveryToastWidgetClass == nullptr)
	{
		return;
	}
	QueuedDeliveryToasts.Add({ Title, Message });
	TryShowQueuedDeliveryToast();
}

void ACSStorePlayerController::ShowNoticeBanner(const FText& Message)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (NoticeBannerWidgetClass == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UI] NoticeBannerWidgetClass가 지정되지 않았습니다."));
		return;
	}

	if (!IsValid(ActiveNoticeBannerWidget))
	{
		ActiveNoticeBannerWidget = CreateWidget<UCSNoticeBannerWidgetBase>(this, NoticeBannerWidgetClass);
		if (!IsValid(ActiveNoticeBannerWidget))
		{
			return;
		}
		ActiveNoticeBannerWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		ActiveNoticeBannerWidget->AddToViewport(500);
	}

	ActiveNoticeBannerWidget->ShowMessage(Message);
}

void ACSStorePlayerController::TryShowQueuedDeliveryToast()
{
	if (!IsLocalPlayerController() || DeliveryToastWidgetClass == nullptr ||
		IsValid(ActiveDeliveryToastWidget) || QueuedDeliveryToasts.IsEmpty() ||
		IsCriticalDeliveryToastUIOpen())
	{
		return;
	}

	const FCSQueuedDeliveryToast NextToast = QueuedDeliveryToasts[0];
	QueuedDeliveryToasts.RemoveAt(0);

	ActiveDeliveryToastWidget = CreateWidget<UCSDeliveryToastWidget>(this, DeliveryToastWidgetClass);
	if (!IsValid(ActiveDeliveryToastWidget))
	{
		return;
	}
	ActiveDeliveryToastWidget->OnToastFinished.AddUniqueDynamic(
		this, &ThisClass::HandleDeliveryToastFinished);
	ActiveDeliveryToastWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	// 발주창은 Viewport 1100번 레이어를 사용하므로, 같은 Viewport의 더 앞 레이어에 표시합니다.
	// Player Screen 레이어에 추가하면 발주창이 토스트보다 앞에 그려질 수 있습니다.
	ActiveDeliveryToastWidget->AddToViewport(1200);
	ActiveDeliveryToastWidget->ShowToast(NextToast.Title, NextToast.Message);
}

void ACSStorePlayerController::HandleDeliveryToastFinished()
{
	if (!IsValid(ActiveDeliveryToastWidget))
	{
		return;
	}

	ActiveDeliveryToastWidget->OnToastFinished.RemoveDynamic(
		this, &ThisClass::HandleDeliveryToastFinished);
	ActiveDeliveryToastWidget->RemoveFromParent();
	ActiveDeliveryToastWidget = nullptr;
	TryShowQueuedDeliveryToast();
}

void ACSStorePlayerController::HandleClosingWarningRequested(int32 RemainingMinutes)
{
	if (!IsLocalPlayerController() || ClosingWarningWidgetClass == nullptr)
	{
		return;
	}

	if (!IsValid(ActiveClosingWarningWidget))
	{
		ActiveClosingWarningWidget = CreateWidget<UCSClosingWarningWidget>(
			this,
			ClosingWarningWidgetClass);
		if (!IsValid(ActiveClosingWarningWidget))
		{
			return;
		}

		ActiveClosingWarningWidget->OnWarningFinished.AddUniqueDynamic(
			this,
			&ACSStorePlayerController::HandleClosingWarningFinished);
		ActiveClosingWarningWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		ActiveClosingWarningWidget->AddToPlayerScreen(50);
	}

	ActiveClosingWarningWidget->ShowWarning(RemainingMinutes);
}

void ACSStorePlayerController::HandleClosingWarningFinished()
{
	if (!IsValid(ActiveClosingWarningWidget))
	{
		return;
	}

	ActiveClosingWarningWidget->OnWarningFinished.RemoveDynamic(
		this,
		&ACSStorePlayerController::HandleClosingWarningFinished);
	ActiveClosingWarningWidget->RemoveFromParent();
	ActiveClosingWarningWidget = nullptr;
}

void ACSStorePlayerController::HandleDailySummaryRequested()
{
	if (!IsSleepTransitionActive()) ShowDailySummary(false);
}

bool ACSStorePlayerController::BeginSleepTransition(AActor* Requester)
{
	if (!IsValid(Requester) || IsSleepTransitionActive()) return false;
	SleepTransitionOwner = Requester;
	ActiveSleepTransitionWidget = nullptr;
	CloseJournal();
	if (IsValid(ActiveDailySummaryWidget) && bDailySummaryReadOnly)
	{
		HandleDailySummaryConfirmed();
	}
	SetStoreHUDHidden(Requester, true);
	SetStoreTopBarHidden(Requester, true);
	FlushPressedKeys();
	UpdateClosingInputMode();
	return true;
}

void ACSStorePlayerController::EndSleepTransition(AActor* Requester)
{
	if (SleepTransitionOwner != Requester) return;
	ActiveSleepTransitionWidget = nullptr;
	SleepTransitionOwner = nullptr;
	SetStoreHUDHidden(Requester, false);
	SetStoreTopBarHidden(Requester, false);
	UpdateClosingInputMode();
	TryShowQueuedDeliveryToast();
	TryShowPendingClosingAnnouncement();
}

void ACSStorePlayerController::SetSleepTransitionWidget(UUserWidget* Widget)
{
	if (!IsSleepTransitionActive()) return;
	ActiveSleepTransitionWidget = Widget;
	UpdateClosingInputMode();
}

void ACSStorePlayerController::ShowSleepDailySummary()
{
	if (!IsSleepTransitionActive() || !IsValid(StoreGameState) ||
		!StoreGameState->bIsDailySummaryPending) return;
	// 수면 화면에서 여는 정산은 조회 전용입니다. 닫아도 다음 날로 넘어가지 않습니다.
	ShowDailySummary(true);
}

void ACSStorePlayerController::ToggleDailySummary()
{
	if (bCraftCloseupActive) return;
	if (IsValid(ActiveDailySummaryWidget))
	{
		// C는 조회용 화면만 닫으며, 실제 마감 정산의 확인을 대신하지 않습니다.
		if (bDailySummaryReadOnly) HandleDailySummaryConfirmed();
		return;
	}
	if (!IsLocalPlayerController() || IsGameplayUIInputLocked() || IsPaused()) return;
	if (!IsValid(StoreGameState) || StoreGameState->CurrentPhase == ECStorePhase::Finished ||
		StoreGameState->CurrentEndOfDayTask != ECEndOfDayTask::None) return;
	ShowDailySummary(true);
}

void ACSStorePlayerController::ShowDailySummary(bool bReadOnly)
{
	if (IsValid(ActiveDailySummaryWidget))
	{
		// 조회 중 실제 마감이 도착하면 같은 화면에서 원래 정산 확인 흐름으로 전환합니다.
		if (!bReadOnly) bDailySummaryReadOnly = false;
		return;
	}
	if (!IsLocalPlayerController() || IsValid(ActiveDailySummaryWidget) || DailySummaryWidgetClass == nullptr) return;
	ActiveDailySummaryWidget = CreateWidget<UCSDailySummaryWidget>(this, DailySummaryWidgetClass);
	if (!IsValid(ActiveDailySummaryWidget)) return;
	bDailySummaryReadOnly = bReadOnly;
	if (bReadOnly) ActiveDailySummaryWidget->SetIsFocusable(true);
	ActiveDailySummaryWidget->OnDailySummaryConfirmed.AddDynamic(this, &ACSStorePlayerController::HandleDailySummaryConfirmed);
	// 수면 전환 화면은 ZOrder 2200으로 표시되므로, 수면 중 정산 조회는 그보다 위에 띄웁니다.
	// 그렇지 않으면 정산 창이 생성되어도 수면 화면 뒤에 가려졌다가 다음 날 전환 시 뒤늦게 보입니다.
	ActiveDailySummaryWidget->AddToViewport(IsSleepTransitionActive() ? 2300 : 0);
	SetStoreHUDHidden(ActiveDailySummaryWidget, true);
	SetGameplayUIInputLock(ActiveDailySummaryWidget, true);
	if (bReadOnly) ActiveDailySummaryWidget->SetKeyboardFocus();
}

void ACSStorePlayerController::HandleDailySummaryConfirmed()
{
	const bool bCompleteClosingSummary = !bDailySummaryReadOnly;
	if (IsValid(ActiveDailySummaryWidget))
	{
		SetGameplayUIInputLock(ActiveDailySummaryWidget, false);
		SetStoreHUDHidden(ActiveDailySummaryWidget, false);
		ActiveDailySummaryWidget->RemoveFromParent();
		ActiveDailySummaryWidget = nullptr;
	}
	bDailySummaryReadOnly = false;
	if (bCompleteClosingSummary)
	{
		if (ACSStoreGameMode* Mode = Cast<ACSStoreGameMode>(GetWorld()->GetAuthGameMode())) Mode->CompleteDailySummary();
	}
}

void ACSStorePlayerController::HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask)
{
	// 필수 마감 업무가 시작되면 수첩을 닫아 해당 업무 화면의 입력을 우선합니다.
	if(NewTask != ECEndOfDayTask::None) CloseJournal();
	UpdateClosingInputMode();
}

void ACSStorePlayerController::HandleStorePhaseChanged(ECStorePhase NewPhase)
{
	if(NewPhase == ECStorePhase::Finished) CloseJournal();
	const bool bBusinessStarted =
		LastObservedStorePhase != ECStorePhase::Open &&
		NewPhase == ECStorePhase::Open;
	const bool bBusinessEnded =
		LastObservedStorePhase == ECStorePhase::Open &&
		NewPhase != ECStorePhase::Open;

	if (bBusinessStarted)
	{
		bClosingAnnouncementPending = false;
		ShowStatusAnnouncement(ECSStatusAnnouncementType::Opened);
	}
	else if (bBusinessEnded)
	{
		bClosingAnnouncementPending = true;
		TryShowPendingClosingAnnouncement();
	}

	LastObservedStorePhase = NewPhase;

	UpdateClosingInputMode();
}

void ACSStorePlayerController::UpdateClosingInputMode()
{
	const bool bIsClosingUIOpen = IsValid(StoreGameState) &&
		(StoreGameState->CurrentEndOfDayTask != ECEndOfDayTask::None ||
			StoreGameState->CurrentPhase == ECStorePhase::Finished);
	const bool bHasGameplayUIInputLock = HasGameplayUIInputLock();
	const bool bShouldUseUIInput = bIsClosingUIOpen || bHasGameplayUIInputLock;

	bShowMouseCursor = !bOpeningPresentationActive && bShouldUseUIInput &&
		(!IsSleepTransitionActive() || IsValid(ActiveSleepTransitionWidget));

	// 단계와 업무 이벤트가 같은 입력 잠금을 여러 번 요청할 수 있으므로 먼저 초기화합니다.
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();

	if (bShouldUseUIInput)
	{
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
	}

	if (bOpeningPresentationActive)
	{
		// 전경 연출과 카메라 복귀가 끝날 때까지 이동과 메뉴 입력을 막습니다.
		SetInputMode(FInputModeUIOnly());
	}
	else if (IsSleepTransitionActive())
	{
		// 날짜/단계 변경 이벤트가 와도 Fade가 끝날 때까지 모든 게임 입력을 차단합니다.
		FInputModeUIOnly InputMode;
		UUserWidget* FocusWidget = IsValid(ActiveDailySummaryWidget)
			? ActiveDailySummaryWidget.Get()
			: ActiveSleepTransitionWidget.Get();
		if (IsValid(FocusWidget)) InputMode.SetWidgetToFocus(FocusWidget->TakeWidget());
		SetInputMode(InputMode);
	}
	else if (bIsClosingUIOpen)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

		if (StoreHUDWidget != nullptr)
		{
			InputMode.SetWidgetToFocus(StoreHUDWidget->TakeWidget());
		}

		SetInputMode(InputMode);
	}
	else if (bHasGameplayUIInputLock)
	{
		// 발주·계산 UI가 열린 동안에는 단계가 변경되어도 캐릭터 조작으로 돌아가지 않게 UI 입력만 받습니다.
		FInputModeUIOnly InputMode;
		if (UUserWidget* FocusedWidget = GetFocusedGameplayUIWidget())
		{
			InputMode.SetWidgetToFocus(FocusedWidget->TakeWidget());
		}

		SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}
}

void ACSStorePlayerController::SetGameplayUIInputLock(
	UUserWidget* RequestingWidget,
	bool bShouldLock)
{
	if (!IsValid(RequestingWidget))
	{
		return;
	}

	if (bShouldLock)
	{
		// 다른 UI가 조작을 잠그면 수첩을 닫아 두 화면의 포커스가 충돌하지 않도록 합니다.
		if(IsValid(ActiveJournalWidget) && RequestingWidget != ActiveJournalWidget) CloseJournal();
		// 같은 위젯이 여러 번 열림을 알리더라도 입력 잠금 목록에는 한 번만 등록합니다.
		GameplayUIInputLockWidgets.AddUnique(RequestingWidget);
	}
	else
	{
		GameplayUIInputLockWidgets.Remove(RequestingWidget);
	}

	// UI가 열리거나 닫히는 즉시 커서, 이동 제한, 포커스를 같은 규칙으로 갱신합니다.
	UpdateClosingInputMode();

	// 이 함수를 거쳐가는 모든 UI 열림/닫힘을 방송
	OnGameplayUIInputLockChanged.Broadcast(RequestingWidget, bShouldLock);
	// 정산 창이 닫히는 순간, 그동안 보류했던 배달 알림을 순서대로 표시합니다.
	if (!bShouldLock)
	{
		TryShowQueuedDeliveryToast();
	}

	// 마감 순간 결제 완료 연출이 재생 중이었다면 계산 UI의 잠금이 풀린 뒤 종료 공지를 띄웁니다.
	if (!bShouldLock)
	{
		TryShowPendingClosingAnnouncement();
	}
}

void ACSStorePlayerController::SetStoreHUDHidden(
	UObject* Requester,
	const bool bShouldHide)
{
	if (!IsValid(Requester))
	{
		return;
	}

	if (bShouldHide)
	{
		if (StoreHUDHideRequesters.IsEmpty())
		{
			if (IsValid(StoreHUDWidget))
			{
				StoreHUDVisibilityBeforeHide = StoreHUDWidget->GetVisibility();
				bHasStoredHUDVisibilityBeforeHide = true;
			}
		}

		StoreHUDHideRequesters.AddUnique(Requester);
		if (IsValid(StoreHUDWidget))
		{
			StoreHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	StoreHUDHideRequesters.Remove(Requester);
	if (StoreHUDHideRequesters.IsEmpty() &&
		IsValid(StoreHUDWidget) &&
		bHasStoredHUDVisibilityBeforeHide)
	{
		StoreHUDWidget->SetVisibility(StoreHUDVisibilityBeforeHide);
		bHasStoredHUDVisibilityBeforeHide = false;
	}
}

void ACSStorePlayerController::SetStoreTopBarHidden(UObject* Requester, bool bShouldHide)
{
	if (!IsValid(Requester)) return;
	if (bShouldHide) StoreTopBarHideRequesters.AddUnique(Requester);
	else StoreTopBarHideRequesters.Remove(Requester);
	if (IsValid(StoreTopBarWidget))
	{
		StoreTopBarWidget->SetVisibility(StoreTopBarHideRequesters.IsEmpty()
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
}

void ACSStorePlayerController::TryShowPendingClosingAnnouncement()
{
	if (!bClosingAnnouncementPending || HasGameplayUIInputLock())
	{
		return;
	}

	bClosingAnnouncementPending = false;
	ShowStatusAnnouncement(ECSStatusAnnouncementType::Closed);
}

bool ACSStorePlayerController::IsGameplayUIInputLocked() const
{
	return HasGameplayUIInputLock();
}

bool ACSStorePlayerController::CanStartUnlockGuide() const
{
	return IsLocalPlayerController() && !IsPaused() && !IsGameplayUIInputLocked() &&
		!bCraftCloseupActive && !IsValid(ActiveTemporaryTitleWidget) &&
		!IsValid(ActiveDailySummaryWidget) && !IsValid(ActiveStatusAnnouncementWidget) &&
		!IsValid(ActiveClosingWarningWidget);
}

bool ACSStorePlayerController::HasGameplayUIInputLock() const
{
	return bOpeningPresentationActive || IsSleepTransitionActive() || GameplayUIInputLockWidgets.ContainsByPredicate(
		[](const TObjectPtr<UUserWidget>& Widget)
		{
			return IsValid(Widget);
		});
}

bool ACSStorePlayerController::IsCriticalDeliveryToastUIOpen() const
{
	return bOpeningPresentationActive || IsSleepTransitionActive() || GameplayUIInputLockWidgets.ContainsByPredicate(
		[](const TObjectPtr<UUserWidget>& Widget)
		{
			return IsValid(Widget) && Widget->IsA(UCSDailySummaryWidget::StaticClass());
		});
}

UUserWidget* ACSStorePlayerController::GetFocusedGameplayUIWidget() const
{
	for (int32 WidgetIndex = GameplayUIInputLockWidgets.Num() - 1;
		WidgetIndex >= 0;
		--WidgetIndex)
	{
		if (IsValid(GameplayUIInputLockWidgets[WidgetIndex]))
		{
			return GameplayUIInputLockWidgets[WidgetIndex];
		}
	}

	return nullptr;
}

void ACSStorePlayerController::ShowPlacementUnavailablePrompt()
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->ShowPlacementUnavailablePrompt();
	}
}

void ACSStorePlayerController::SetInteractionPrompt(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetInteractionPrompt(Message, TextColor);
	}
}

void ACSStorePlayerController::SetProductPrice(const FText& Message)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetProductPrice(Message);
	}
}

void ACSStorePlayerController::SetProductShelfLife(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetProductShelfLife(Message, TextColor);
	}
}

void ACSStorePlayerController::SetProductFreshness(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetProductFreshness(Message, TextColor);
	}
}

void ACSStorePlayerController::SetStorageWarning(const FText& Message)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetStorageWarning(Message);
	}
}

void ACSStorePlayerController::SetInteractionPrompts(const TArray<FCSInteractionPrompt>& Prompts)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetInteractionPrompts(Prompts);
	}
}

void ACSStorePlayerController::ClearInteractionContext()
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->ClearInteractionContext();
	}
}

void ACSStorePlayerController::SetDefaultCrosshairVisible(bool bVisible)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetDefaultCrosshairVisible(bVisible);
	}
}

void ACSStorePlayerController::SetBuildModeHUDState(
	bool bIsBuildMode,
	bool bPlayTransition)
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetBuildModeActive(bIsBuildMode, bPlayTransition);
	}
	if (IsValid(StoreTopBarWidget))
	{
		StoreTopBarWidget->SetBuildModeActive(bIsBuildMode, bPlayTransition);
	}
}

void ACSStorePlayerController::SetQuickbarHUDData(
	int32 SlotCount,
	int32 SelectedSlotIndex,
	const TArray<FText>& ToolNames,
	const TArray<UTexture2D*>& ToolIcons)
{
	CachedQuickbarSlotCount = SlotCount;
	CachedQuickbarSelectedSlotIndex = SelectedSlotIndex;
	CachedQuickbarToolNames = ToolNames;
	CachedQuickbarToolIcons.Reset();
	CachedQuickbarToolIcons.Reserve(ToolIcons.Num());
	for (UTexture2D* ToolIcon : ToolIcons)
	{
		CachedQuickbarToolIcons.Add(ToolIcon);
	}

	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->SetQuickbarData(
			SlotCount,
			SelectedSlotIndex,
			ToolNames,
			ToolIcons);
	}
}
