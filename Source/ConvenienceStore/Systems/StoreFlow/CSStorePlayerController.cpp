#include "Systems/StoreFlow/CSStorePlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/HUD/CSStoreHUDWidget.h"
#include "UI/Store/CSDailySummaryWidget.h"
#include "UI/Store/CSStatusAnnouncementWidget.h"
#include "UI/Store/CSClosingWarningWidget.h"
#include "Features/UI/CSDeliveryToastWidget.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"

void ACSStorePlayerController::BeginPlay()
{
	Super::BeginPlay();

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
}

// === Temporary Title Screen: 정식 타이틀 도입 시 아래 세 함수 전체 제거 ===
void ACSStorePlayerController::ShowTemporaryTitleScreen()
{
	if (!bUseTemporaryTitleScreen || !IsLocalPlayerController() ||
		TemporaryTitleWidgetClass.IsNull() || IsValid(ActiveTemporaryTitleWidget))
	{
		return;
	}

	UClass* LoadedTitleWidgetClass = TemporaryTitleWidgetClass.LoadSynchronous();
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
	SetGameplayUIInputLock(ActiveTemporaryTitleWidget, true);
	SetPause(true);
}

void ACSStorePlayerController::StartGameFromTemporaryTitle()
{
	CloseTemporaryTitleScreen();
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
	TitleWidgetToClose->RemoveFromParent();
	ActiveTemporaryTitleWidget = nullptr;
}
// === End Temporary Title Screen ===

void ACSStorePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
	if (!IsLocalPlayerController() || IsValid(ActiveDailySummaryWidget) || DailySummaryWidgetClass == nullptr) return;
	ActiveDailySummaryWidget = CreateWidget<UCSDailySummaryWidget>(this, DailySummaryWidgetClass);
	if (!IsValid(ActiveDailySummaryWidget)) return;
	ActiveDailySummaryWidget->OnDailySummaryConfirmed.AddDynamic(this, &ACSStorePlayerController::HandleDailySummaryConfirmed);
	ActiveDailySummaryWidget->AddToViewport();
	SetStoreHUDHidden(ActiveDailySummaryWidget, true);
	SetGameplayUIInputLock(ActiveDailySummaryWidget, true);
}

void ACSStorePlayerController::HandleDailySummaryConfirmed()
{
	if (IsValid(ActiveDailySummaryWidget))
	{
		SetGameplayUIInputLock(ActiveDailySummaryWidget, false);
		SetStoreHUDHidden(ActiveDailySummaryWidget, false);
		ActiveDailySummaryWidget->RemoveFromParent();
		ActiveDailySummaryWidget = nullptr;
	}
	if (ACSStoreGameMode* Mode = Cast<ACSStoreGameMode>(GetWorld()->GetAuthGameMode())) Mode->CompleteDailySummary();
}

void ACSStorePlayerController::HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask)
{
	UpdateClosingInputMode();
}

void ACSStorePlayerController::HandleStorePhaseChanged(ECStorePhase NewPhase)
{
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

	bShowMouseCursor = bShouldUseUIInput;

	// 단계와 업무 이벤트가 같은 입력 잠금을 여러 번 요청할 수 있으므로 먼저 초기화합니다.
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();

	if (bShouldUseUIInput)
	{
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
	}

	if (bIsClosingUIOpen)
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

bool ACSStorePlayerController::HasGameplayUIInputLock() const
{
	return GameplayUIInputLockWidgets.ContainsByPredicate(
		[](const TObjectPtr<UUserWidget>& Widget)
		{
			return IsValid(Widget);
		});
}

bool ACSStorePlayerController::IsCriticalDeliveryToastUIOpen() const
{
	return GameplayUIInputLockWidgets.ContainsByPredicate(
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

void ACSStorePlayerController::ClearInteractionContext()
{
	if (IsValid(StoreHUDWidget))
	{
		StoreHUDWidget->ClearInteractionContext();
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
