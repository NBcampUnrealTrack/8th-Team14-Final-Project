#include "UI/HUD/CSStoreHUDWidget.h"

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

void UCSStoreHUDWidget::SetInteractionPrompt(
	const FText& Message,
	const FLinearColor& TextColor)
{
	if (IsValid(InteractionPanel))
	{
		InteractionPanel->SetInteractionPrompt(Message, TextColor);
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

	if (bPlayTransition)
	{
		OnBuildModeTransitionRequested();
	}
}

void UCSStoreHUDWidget::HandlePhaseChanged(ECStorePhase NewPhase)
{
	UpdatePhaseNameDisplay();
	UpdatePhaseSelection();

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

	// 영업 단계에서 입간판으로 상태를 바꾸면 HUD 문구도 즉시 함께 갱신합니다.
	UpdatePhaseNameDisplay();
	UpdatePhaseSelection();
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
	// 실제 시간은 유지하고 HUD 숫자만 직전 10분 단위로 표시합니다.
	const int32 DisplayMinutes =
		(FMath::FloorToInt(FMath::Max(0.0f, NewGameMinutes)) / 10) * 10;
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
		ClockText->SetText(FormatGameClock(NewGameMinutes));
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
		if (StoreGameState->bIsStoreOpen)
		{
			return 55.0f;
		}

		return StoreGameState->bHasStoreOpenedToday ? 115.0f : 0.0f;
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

	// 영업 가능 시간에는 단계명이 아닌 실제 영업 상태를 우선 표시합니다.
	if (StoreGameState->CurrentPhase == ECStorePhase::Open)
	{
		if (StoreGameState->bIsStoreOpen)
		{
			PhaseNameText->SetText(NSLOCTEXT("Store", "BusinessOpen", "영업중"));
		}
		else
		{
			// 오늘 영업을 시작하지 않았다면 종료가 아닌 개점 대기 상태로 안내합니다.
			PhaseNameText->SetText(StoreGameState->bHasStoreOpenedToday
				? NSLOCTEXT("Store", "BusinessClosed", "영업 종료")
				: NSLOCTEXT("Store", "AwaitingOpening", "개점 대기"));
		}
		return;
	}

	PhaseNameText->SetText(StoreGameState->CurrentPhaseDisplayName);
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
			NSLOCTEXT("Store", "DailyProfit", "오늘 이익: {0} 골드"),
			FText::AsNumber(StoreGameState->DailyProfitGold)));
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
