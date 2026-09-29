#include "UI/HUD/CSStoreHUDWidget.h"

#include "Animation/WidgetAnimation.h"
#include "UI/HUD/CSQuickbarWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/HUD/CSInteractionPanelWidget.h"

void UCSStoreHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreGameState == nullptr)
	{
		return;
	}

	if (IsValid(ClockText))
	{
		InitialClockColor = ClockText->GetColorAndOpacity().GetSpecifiedColor();
	}
	if (IsValid(ClockHourText))
	{
		InitialClockHourColor = ClockHourText->GetColorAndOpacity().GetSpecifiedColor();
	}
	if (IsValid(ClockColonText))
	{
		InitialClockColonColor = ClockColonText->GetColorAndOpacity().GetSpecifiedColor();
	}
	if (IsValid(ClockMinuteText))
	{
		InitialClockMinuteColor = ClockMinuteText->GetColorAndOpacity().GetSpecifiedColor();
	}

	StoreGameState->OnStorePhaseChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandlePhaseChanged);
	StoreGameState->OnStoreOpenStateChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleStoreOpenStateChanged);
	StoreGameState->OnRemainingTimeChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleRemainingTimeChanged);
	StoreGameState->OnGameTimeChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleGameTimeChanged);
	StoreGameState->OnGameClockRunningChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleGameClockRunningChanged);
	StoreGameState->OnFundsChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleFundsChanged);
	StoreGameState->OnReputationChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleReputationChanged);
	StoreGameState->OnReputationGradeChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleReputationGradeChanged);
	StoreGameState->OnBusinessDayStarted.AddDynamic(
		this, &UCSStoreHUDWidget::HandleBusinessDayStarted);
	// 세이브 복원으로 바뀐 영업일도 같은 표시 갱신 경로로 처리
	StoreGameState->OnBusinessDayRestored.AddDynamic(
		this, &UCSStoreHUDWidget::HandleBusinessDayStarted);
	StoreGameState->OnStoreExperienceChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleStoreExperienceChanged);
	StoreGameState->OnStoreLevelChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleStoreLevelChanged);
	StoreGameState->OnEndOfDayTaskChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleEndOfDayTaskChanged);
	StoreGameState->OnDailySalesChanged.AddDynamic(
		this, &UCSStoreHUDWidget::HandleDailySalesChanged);

	if (CompleteEndOfDayTaskButton != nullptr)
	{
		CompleteEndOfDayTaskButton->OnClicked.AddDynamic(
			this, &UCSStoreHUDWidget::CompleteEndOfDayTask);
	}

	HandlePhaseChanged(StoreGameState->CurrentPhase);
	UpdateCustomerEntryBanner(false);
	HandleRemainingTimeChanged(StoreGameState->RemainingSeconds);
	HandleGameTimeChanged(StoreGameState->CurrentGameMinutes);
	HandleGameClockRunningChanged(StoreGameState->bIsGameClockRunning);
	HandleFundsChanged(StoreGameState->GetCurrentFunds());
	HandleReputationChanged(StoreGameState->GetReputationScore());
	HandleReputationGradeChanged(StoreGameState->GetReputationGrade());
	HandleBusinessDayStarted(StoreGameState->CurrentBusinessDay);
	UpdateStoreProgressDisplay();
	HandleEndOfDayTaskChanged(StoreGameState->CurrentEndOfDayTask);
	UpdateDailySalesDisplay();

	if (ClockPanel != nullptr)
	{
		// 시간은 준비·영업·마감 단계의 기준 정보이므로 HUD에 항상 표시합니다.
		ClockPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// HUD가 생성될 때 이전 플레이 세션의 조준 안내가 보이지 않도록 초기화합니다.
	ClearInteractionContext();
}

void UCSStoreHUDWidget::NativeDestruct()
{
	GetWorld()->GetTimerManager().ClearTimer(BuildModeBorderExitTimerHandle);
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandlePhaseChanged);
		StoreGameState->OnStoreOpenStateChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleStoreOpenStateChanged);
		StoreGameState->OnRemainingTimeChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleRemainingTimeChanged);
		StoreGameState->OnGameTimeChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleGameTimeChanged);
		StoreGameState->OnGameClockRunningChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleGameClockRunningChanged);
		StoreGameState->OnFundsChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleFundsChanged);
		StoreGameState->OnReputationChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleReputationChanged);
		StoreGameState->OnReputationGradeChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleReputationGradeChanged);
		StoreGameState->OnBusinessDayStarted.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleBusinessDayStarted);
		StoreGameState->OnBusinessDayRestored.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleBusinessDayStarted);
		StoreGameState->OnStoreExperienceChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleStoreExperienceChanged);
		StoreGameState->OnStoreLevelChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleStoreLevelChanged);
		StoreGameState->OnEndOfDayTaskChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleEndOfDayTaskChanged);
		StoreGameState->OnDailySalesChanged.RemoveDynamic(
			this, &UCSStoreHUDWidget::HandleDailySalesChanged);
	}

	if (CompleteEndOfDayTaskButton != nullptr)
	{
		CompleteEndOfDayTaskButton->OnClicked.RemoveDynamic(
			this, &UCSStoreHUDWidget::CompleteEndOfDayTask);
	}

	Super::NativeDestruct();
}

void UCSStoreHUDWidget::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bBuildModeGlowActive || !IsValid(Image_BuildModeGlow))
	{
		return;
	}

	BuildModeGlowElapsedSeconds += InDeltaTime;
	const float SafePulseSeconds = FMath::Max(0.1f, BuildModeGlowPulseSeconds);
	const float PulseAlpha = 0.5f - 0.5f * FMath::Cos(
		2.0f * PI * BuildModeGlowElapsedSeconds / SafePulseSeconds);
	Image_BuildModeGlow->SetRenderOpacity(FMath::Lerp(
		BuildModeGlowMinOpacity,
		BuildModeGlowMaxOpacity,
		PulseAlpha));
}

void UCSStoreHUDWidget::SetInteractionPrompt(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->SetInteractionPrompt(Message, TextColor);
	}
}

void UCSStoreHUDWidget::SetInteractionPrompts(const TArray<FCSInteractionPrompt>& Prompts)
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->SetInteractionPrompts(Prompts);
	}
}

void UCSStoreHUDWidget::ShowPlacementUnavailablePrompt()
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->ClearInteractionContext();
		InteractionPanel->SetInteractionPrompt(
			NSLOCTEXT("Interaction", "ShelfLevelFull", "조준한 단에 빈 슬롯이 없습니다."),
			FLinearColor(1.0f, 0.25f, 0.25f, 1.0f));
	}
}

void UCSStoreHUDWidget::SetProductPrice(const FText& Message)
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->SetProductPrice(Message);
	}
}

void UCSStoreHUDWidget::SetProductShelfLife(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->SetProductShelfLife(Message, TextColor);
	}
}

void UCSStoreHUDWidget::SetProductFreshness(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->SetProductFreshness(Message, TextColor);
	}
}

void UCSStoreHUDWidget::SetStorageWarning(const FText& Message)
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->SetStorageWarning(Message);
	}
}

void UCSStoreHUDWidget::ClearInteractionContext()
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->ClearInteractionContext();
	}
}

void UCSStoreHUDWidget::SetBuildModeActive(
	bool bIsBuildMode,
	bool bPlayTransition)
{
	bBuildModeGlowActive = bIsBuildMode;
	BuildModeGlowElapsedSeconds = 0.0f;
	if (IsValid(Image_BuildModeGlow))
	{
		Image_BuildModeGlow->SetRenderOpacity(BuildModeGlowMinOpacity);
	}

	// 이전 건축 모드 안내는 새 테두리 연출로 교체합니다.
	if (UWidget* LegacyIndicator = GetWidgetFromName(TEXT("BuildModeIndicatorPanel")))
	{
		LegacyIndicator->SetVisibility(ESlateVisibility::Collapsed);
	}

	UWidget* BuildModeHUD = GetWidgetFromName(TEXT("CanvasPanel_BuildModeHUD"));
	GetWorld()->GetTimerManager().ClearTimer(BuildModeBorderExitTimerHandle);
	if (bIsBuildMode)
	{
		if (IsValid(BuildModeHUD))
		{
			BuildModeHUD->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (bPlayTransition && IsValid(Anim_BuildModeBorder))
		{
			StopAnimation(Anim_BuildModeBorder);
			PlayAnimation(
				Anim_BuildModeBorder,
				0.0f,
				1,
				EUMGSequencePlayMode::Forward,
				1.0f,
				false);
		}
	}
	else if (bPlayTransition && IsValid(Anim_BuildModeBorder) && IsValid(BuildModeHUD))
	{
		StopAnimation(Anim_BuildModeBorder);
		PlayAnimation(
			Anim_BuildModeBorder,
			0.0f,
			1,
			EUMGSequencePlayMode::Reverse,
			1.0f,
			false);
		const float Duration = FMath::Max(
			0.01f,
			Anim_BuildModeBorder->GetEndTime() - Anim_BuildModeBorder->GetStartTime());
		GetWorld()->GetTimerManager().SetTimer(
			BuildModeBorderExitTimerHandle,
			this,
			&ThisClass::FinishBuildModeBorderExit,
			Duration,
			false);
	}
	else
	{
		FinishBuildModeBorderExit();
	}

	const FText NoticeText = bIsBuildMode
		? NSLOCTEXT("StoreHUD", "BuildModeOn", "건축 모드 ON")
		: NSLOCTEXT("StoreHUD", "BusinessModeOn", "영업 모드 ON");
	if (BuildModeNoticeText != nullptr)
	{
		BuildModeNoticeText->SetText(NoticeText);
	}

	UTexture2D* ModeIconTexture = bIsBuildMode
		? BuildModeIconTexture
		: BusinessModeIconTexture;
	if (BuildModeIconImage != nullptr && ModeIconTexture != nullptr)
	{
		BuildModeIconImage->SetBrushFromTexture(ModeIconTexture, false);
	}

}

void UCSStoreHUDWidget::FinishBuildModeBorderExit()
{
	if (UWidget* BuildModeHUD = GetWidgetFromName(TEXT("CanvasPanel_BuildModeHUD")))
	{
		BuildModeHUD->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCSStoreHUDWidget::SetQuickbarData(
	int32 SlotCount,
	int32 SelectedSlotIndex,
	const TArray<FText>& ToolNames,
	const TArray<UTexture2D*>& ToolIcons)
{
	if (IsValid(QuickbarWidget))
	{
		QuickbarWidget->SetQuickbarData(
			SlotCount,
			SelectedSlotIndex,
			ToolNames,
			ToolIcons);
	}
}

void UCSStoreHUDWidget::SetDefaultCrosshairVisible(bool bVisible)
{
	if (IsValid(CrosshairDot))
	{
		CrosshairDot->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UCSStoreHUDWidget::HandlePhaseChanged(ECStorePhase NewPhase)
{
	UpdatePhaseNameDisplay();
	UpdatePhaseSelection();
	UpdateCustomerEntryBanner(false);

	if (NewPhase == ECStorePhase::Settlement || NewPhase == ECStorePhase::Finished)
	{
		// 조준점이 숨겨지는 단계에서는 이전 배치 안내도 함께 숨깁니다.
		ClearInteractionContext();
	}

	UpdateEndOfDayPanel();
}

void UCSStoreHUDWidget::HandleStoreOpenStateChanged(bool bNewStoreOpen)
{
	(void)bNewStoreOpen;

	// 최초 개점에서는 Phase 변경 직후 bHasStoreOpenedToday가 갱신되므로
	// 메인 영업 표시도 다시 평가해야 합니다. 영업 중 입간판만 돌린 경우에는
	// 표시 로직상 메인 상태는 그대로이고 입장 상태 배너만 달라집니다.
	UpdatePhaseNameDisplay();
	UpdatePhaseSelection();
	UpdateCustomerEntryBanner(true);
}

void UCSStoreHUDWidget::HandleRemainingTimeChanged(float NewRemainingSeconds)
{
	if (RemainingTimeText != nullptr)
	{
		RemainingTimeText->SetText(FormatTime(NewRemainingSeconds));
	}
}

void UCSStoreHUDWidget::HandleGameTimeChanged(float NewGameMinutes)
{
	const float CountdownDuration = FMath::Max(
		1.0f,
		StoreGameState->ClosingClockCountdownMinutes);
	const float CountdownStartMinutes =
		StoreGameState->AutomaticClosingTimeMinutes - CountdownDuration;
	const bool bIsClosingCountdown =
		NewGameMinutes >= CountdownStartMinutes &&
		NewGameMinutes <= StoreGameState->AutomaticClosingTimeMinutes;

	const int32 WholeGameMinutes = FMath::FloorToInt(FMath::Max(0.0f, NewGameMinutes));
	const int32 DisplayMinutes = bIsClosingCountdown
		? WholeGameMinutes
		: (WholeGameMinutes / 10) * 10;
	const int32 WrappedMinutes = DisplayMinutes % 1440;
	const int32 Hour24 = WrappedMinutes / 60;
	const int32 Minutes = WrappedMinutes % 60;

	if (ClockHourText != nullptr)
	{
		ClockHourText->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Hour24)));
	}

	if (ClockColonText != nullptr)
	{
		ClockColonText->SetText(FText::FromString(TEXT(":")));
	}

	if (ClockMinuteText != nullptr)
	{
		ClockMinuteText->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Minutes)));
	}

	if (ClockText != nullptr)
	{
		ClockText->SetText(FormatGameClock(static_cast<float>(DisplayMinutes)));
	}

	const int32 ElapsedCountdownMinutes = bIsClosingCountdown
		? FMath::Clamp(
			WholeGameMinutes - FMath::CeilToInt(CountdownStartMinutes),
			0,
			FMath::CeilToInt(CountdownDuration))
		: 0;
	const float ClosingColorAlpha = bIsClosingCountdown
		? static_cast<float>(ElapsedCountdownMinutes) / CountdownDuration
		: 0.0f;
	ApplyClosingClockColor(FMath::Clamp(ClosingColorAlpha, 0.0f, 1.0f));
}

void UCSStoreHUDWidget::ApplyClosingClockColor(float ClosingColorAlpha)
{
	if (IsValid(ClockText))
	{
		ClockText->SetColorAndOpacity(FMath::Lerp(
			InitialClockColor,
			ClosingClockColor,
			ClosingColorAlpha));
	}
	if (IsValid(ClockHourText))
	{
		ClockHourText->SetColorAndOpacity(FMath::Lerp(
			InitialClockHourColor,
			ClosingClockColor,
			ClosingColorAlpha));
	}
	if (IsValid(ClockColonText))
	{
		ClockColonText->SetColorAndOpacity(FMath::Lerp(
			InitialClockColonColor,
			ClosingClockColor,
			ClosingColorAlpha));
	}
	if (IsValid(ClockMinuteText))
	{
		ClockMinuteText->SetColorAndOpacity(FMath::Lerp(
			InitialClockMinuteColor,
			ClosingClockColor,
			ClosingColorAlpha));
	}
}

void UCSStoreHUDWidget::HandleGameClockRunningChanged(bool bNewClockRunning)
{
	OnClockTickStateChanged(bNewClockRunning);

	if (!bNewClockRunning && ClockColonText != nullptr)
	{
		// 정지 시 애니메이션의 마지막 프레임과 무관하게 완전 불투명으로 복구합니다.
		ClockColonText->SetRenderOpacity(1.0f);
	}
}

void UCSStoreHUDWidget::HandleFundsChanged(int32 NewFunds)
{
	if (FundsText != nullptr)
	{
		FundsText->SetText(FText::Format(
			NSLOCTEXT("StoreHUD", "FundsFormat", "{0}G"),
			FText::AsNumber(NewFunds)));
	}
}

void UCSStoreHUDWidget::HandleReputationChanged(int32 NewReputationScore)
{
	if (ReputationScoreText != nullptr)
	{
		ReputationScoreText->SetText(FText::AsNumber(NewReputationScore));
	}
}

void UCSStoreHUDWidget::HandleReputationGradeChanged(
	ECSReputationGrade NewReputationGrade)
{
	if (ReputationGradeText != nullptr)
	{
		ReputationGradeText->SetText(
			GetReputationGradeDisplayName(NewReputationGrade));
	}
}

void UCSStoreHUDWidget::HandleBusinessDayStarted(int32 NewBusinessDay)
{
	if (BusinessDayText != nullptr)
	{
		BusinessDayText->SetText(FText::Format(
			NSLOCTEXT("StoreHUD", "BusinessDayFormat", "{0} 일차"),
			FText::AsNumber(NewBusinessDay)));
	}
}

void UCSStoreHUDWidget::HandleStoreExperienceChanged(int32 NewExperience)
{
	(void)NewExperience;
	UpdateStoreProgressDisplay();
}

void UCSStoreHUDWidget::HandleStoreLevelChanged(int32 NewLevel)
{
	(void)NewLevel;
	UpdateStoreProgressDisplay();
}

void UCSStoreHUDWidget::HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask)
{
	UpdateEndOfDayPanel();
}

void UCSStoreHUDWidget::HandleDailySalesChanged()
{
	UpdateDailySalesDisplay();
}

float UCSStoreHUDWidget::GetPhaseSelectionTranslationY() const
{
	if (!IsValid(StoreGameState))
	{
		return 0.0f;
	}

	if (StoreGameState->CurrentPhase == ECStorePhase::Preparation)
	{
		return 0.0f;
	}

	if (StoreGameState->CurrentPhase == ECStorePhase::Open)
	{
		return StoreGameState->bHasStoreOpenedToday ? 55.0f : 0.0f;
	}

	// 마감 자유 활동부터 다음 날 시작 전까지는 영업 종료 위치를 유지합니다.
	return 115.0f;
}

void UCSStoreHUDWidget::UpdatePhaseSelection()
{
	const float TargetTranslationY = GetPhaseSelectionTranslationY();
	// 다음 날 위로 돌아갈 때는 즉시 배치하고 진행 방향인 아래쪽 이동만 재생합니다.
	const bool bShouldAnimate = bPhaseSelectionInitialized &&
		TargetTranslationY > CurrentPhaseSelectionY;

	OnPhaseSelectionChanged(TargetTranslationY, bShouldAnimate);
	CurrentPhaseSelectionY = TargetTranslationY;
	bPhaseSelectionInitialized = true;
}

void UCSStoreHUDWidget::UpdatePhaseNameDisplay()
{
	if (!IsValid(PhaseNameText) || !IsValid(StoreGameState))
	{
		return;
	}

	// 입간판을 닫아도 영업 단계 자체는 계속 유지합니다.
	if (StoreGameState->CurrentPhase == ECStorePhase::Open)
	{
		PhaseNameText->SetText(StoreGameState->bHasStoreOpenedToday
			? NSLOCTEXT("Store", "BusinessOpen", "영업중")
			: NSLOCTEXT("Store", "AwaitingOpening", "개점 대기"));
		return;
	}

	PhaseNameText->SetText(StoreGameState->CurrentPhaseDisplayName);
}

void UCSStoreHUDWidget::UpdateCustomerEntryBanner(bool bAnimate)
{
	if (!IsValid(StoreGameState))
	{
		return;
	}

	if (IsValid(CustomerEntryBanner))
	{
		// 준비·영업·마감 단계 모두 입간판 상태 배너를 유지합니다.
		CustomerEntryBanner->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	const bool bEntryEnabled = StoreGameState->bIsStoreOpen;
	const FLinearColor TextColor = bEntryEnabled
		? EntryOpenTextColor
		: EntryPausedTextColor;
	const FLinearColor BannerColor = bEntryEnabled
		? EntryOpenBannerColor
		: EntryPausedBannerColor;

	if (IsValid(CustomerEntryStatusText))
	{
		CustomerEntryStatusText->SetText(bEntryEnabled
			? NSLOCTEXT("StoreHUD", "CustomerEntryEnabled", "입장\n가능")
			: NSLOCTEXT("StoreHUD", "CustomerEntryPaused", "잠시\n휴점"));
		CustomerEntryStatusText->SetColorAndOpacity(TextColor);
	}

	if (IsValid(CustomerEntryShortStatusText))
	{
		CustomerEntryShortStatusText->SetText(bEntryEnabled
			? NSLOCTEXT("StoreHUD", "CustomerEntryOpenShort", "OPEN")
			: NSLOCTEXT("StoreHUD", "CustomerEntryClosedShort", "CLOSED"));
		CustomerEntryShortStatusText->SetColorAndOpacity(TextColor);
	}

	if (IsValid(Image_BannerBackground))
	{
		Image_BannerBackground->SetColorAndOpacity(BannerColor);
	}

	OnCustomerEntryStatusChanged(bEntryEnabled, bAnimate);
}

void UCSStoreHUDWidget::UpdateStoreProgressDisplay()
{
	if (!IsValid(StoreGameState))
	{
		return;
	}

	if (StoreLevelText != nullptr)
	{
		StoreLevelText->SetText(FText::AsNumber(StoreGameState->GetStoreLevel()));
	}

	if (StoreLevelNameText != nullptr)
	{
		StoreLevelNameText->SetText(StoreGameState->GetStoreLevelDisplayName());
	}

	if (StoreExperienceProgressBar != nullptr)
	{
		StoreExperienceProgressBar->SetPercent(StoreGameState->GetStoreLevelProgress());
	}

	if (StoreExperienceText == nullptr)
	{
		return;
	}

	if (StoreGameState->IsMaximumStoreLevel())
	{
		StoreExperienceText->SetText(
			NSLOCTEXT("StoreHUD", "MaximumStoreLevel", "MAX LEVEL"));
		return;
	}

	StoreExperienceText->SetText(FText::Format(
		NSLOCTEXT("StoreHUD", "StoreExperienceFormat", "{0} / {1} EXP"),
		FText::AsNumber(StoreGameState->GetStoreExperienceInCurrentLevel()),
		FText::AsNumber(StoreGameState->GetStoreExperienceRequiredForNextLevel())));
}

void UCSStoreHUDWidget::UpdateEndOfDayPanel()
{
	if (StoreGameState == nullptr)
	{
		return;
	}

	const ECEndOfDayTask CurrentTask = StoreGameState->CurrentEndOfDayTask;
	const bool bIsNextDayReady =
		CurrentTask == ECEndOfDayTask::None &&
		StoreGameState->CurrentPhase == ECStorePhase::Finished;
	const bool bHasTask = CurrentTask != ECEndOfDayTask::None;
	const bool bShowPanel = bHasTask || bIsNextDayReady;

	if (EndOfDayPanel != nullptr)
	{
		EndOfDayPanel->SetVisibility(bShowPanel
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}

	if (EndOfDayTaskText != nullptr)
	{
		EndOfDayTaskText->SetText(bIsNextDayReady
			? NSLOCTEXT("Store", "NextDayReady", "02:00 AM — 다음 날을 시작하세요.")
			: GetEndOfDayButtonText(CurrentTask));
	}

	if (CompleteEndOfDayTaskButton != nullptr)
	{
		CompleteEndOfDayTaskButton->SetVisibility(bShowPanel
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}

	if (CompleteEndOfDayTaskButtonText != nullptr)
	{
		CompleteEndOfDayTaskButtonText->SetText(bIsNextDayReady
			? NSLOCTEXT("Store", "StartNextDay", "다음 날 시작")
			: NSLOCTEXT("Store", "CompleteTask", "완료"));
	}
}

void UCSStoreHUDWidget::UpdateDailySalesDisplay()
{
	if (StoreGameState == nullptr)
	{
		return;
	}

	// HUD 블루프린트에 해당 텍스트가 배치된 경우에만 오늘의 판매 요약을 표시합니다.
	if (DailySalesText != nullptr)
	{
		DailySalesText->SetText(FText::Format(
			NSLOCTEXT("Store", "DailySales", "오늘 매출: {0} 골드"),
			FText::AsNumber(StoreGameState->DailySalesGold)));
	}

	if (DailyProfitText != nullptr)
	{
		DailyProfitText->SetText(FText::Format(
			NSLOCTEXT("Store", "DailyNetProfit", "오늘 순수익: {0} 골드"),
			FText::AsNumber(StoreGameState->GetDailyNetProfitGold())));
	}

	if (DailySoldQuantityText != nullptr)
	{
		DailySoldQuantityText->SetText(FText::Format(
			NSLOCTEXT("Store", "DailySoldQuantity", "판매 수량: {0}"),
			FText::AsNumber(StoreGameState->DailySoldQuantity)));
	}
}

void UCSStoreHUDWidget::CompleteEndOfDayTask()
{
	if (ACSStoreGameMode* StoreGameMode =
		Cast<ACSStoreGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (StoreGameState != nullptr &&
			StoreGameState->CurrentEndOfDayTask == ECEndOfDayTask::None &&
			StoreGameState->CurrentPhase == ECStorePhase::Finished)
		{
			StoreGameMode->StartNextBusinessDay();
		}
		else
		{
			StoreGameMode->CompleteCurrentEndOfDayTask();
		}
	}
}

FText UCSStoreHUDWidget::FormatTime(float Seconds) const
{
	const int32 TotalSeconds = FMath::CeilToInt(FMath::Max(0.0f, Seconds));
	const int32 Minutes = TotalSeconds / 60;
	const int32 RemainingSeconds = TotalSeconds % 60;

	return FText::FromString(FString::Printf(
		TEXT("%02d:%02d"), Minutes, RemainingSeconds));
}

FText UCSStoreHUDWidget::FormatGameClock(float GameMinutes) const
{
	// 분리된 시계가 없는 기존 WBP도 동일하게 10분 단위로 표시합니다.
	const int32 TotalMinutes =
		(FMath::FloorToInt(FMath::Max(0.0f, GameMinutes)) / 10 * 10) % 1440;
	const int32 Hour24 = TotalMinutes / 60;
	const int32 Minutes = TotalMinutes % 60;

	return FText::FromString(FString::Printf(
		TEXT("%02d:%02d"),
		Hour24,
		Minutes));
}

FText UCSStoreHUDWidget::GetReputationGradeDisplayName(
	ECSReputationGrade ReputationGrade) const
{
	switch (ReputationGrade)
	{
	case ECSReputationGrade::VeryBad:
		return NSLOCTEXT("StoreHUD", "ReputationVeryBad", "Very Bad");
	case ECSReputationGrade::Bad:
		return NSLOCTEXT("StoreHUD", "ReputationBad", "Bad");
	case ECSReputationGrade::Normal:
		return NSLOCTEXT("StoreHUD", "ReputationNormal", "Normal");
	case ECSReputationGrade::Good:
		return NSLOCTEXT("StoreHUD", "ReputationGood", "Good");
	case ECSReputationGrade::Excellent:
		return NSLOCTEXT("StoreHUD", "ReputationExcellent", "Excellent");
	case ECSReputationGrade::None:
	default:
		return FText::GetEmpty();
	}
}

FText UCSStoreHUDWidget::GetEndOfDayButtonText(ECEndOfDayTask Task) const
{
	switch (Task)
	{
	case ECEndOfDayTask::OrderReview:
		return NSLOCTEXT("Store", "OrderReview", "발주 확인 완료");
	case ECEndOfDayTask::Settlement:
		return NSLOCTEXT("Store", "Settlement", "정산 완료");
	case ECEndOfDayTask::SalesReport:
		return NSLOCTEXT("Store", "SalesReport", "판매 통계 확인");
	case ECEndOfDayTask::ReputationUpdate:
		return NSLOCTEXT("Store", "ReputationUpdate", "평판 변화 확인");
	case ECEndOfDayTask::None:
	default:
		return FText::GetEmpty();
	}
}
