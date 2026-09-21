#include "UI/HUD/CSStoreTopBarWidget.h"

#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "TimerManager.h"

void UCSStoreTopBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState)) return;

	if (IsValid(ClockText)) InitialClockColor = ClockText->GetColorAndOpacity().GetSpecifiedColor();
	if (IsValid(ClockHourText)) InitialClockHourColor = ClockHourText->GetColorAndOpacity().GetSpecifiedColor();
	if (IsValid(ClockColonText)) InitialClockColonColor = ClockColonText->GetColorAndOpacity().GetSpecifiedColor();
	if (IsValid(ClockMinuteText)) InitialClockMinuteColor = ClockMinuteText->GetColorAndOpacity().GetSpecifiedColor();
	CurrentStatusWidth = ShortStatusWidth;
	TargetStatusWidth = ShortStatusWidth;
	if (IsValid(StatusSizeBox)) StatusSizeBox->SetWidthOverride(ShortStatusWidth);

	StoreGameState->OnStorePhaseChanged.AddUniqueDynamic(this, &ThisClass::HandlePhaseChanged);
	StoreGameState->OnStoreOpenStateChanged.AddUniqueDynamic(this, &ThisClass::HandleStoreOpenStateChanged);
	StoreGameState->OnGameTimeChanged.AddUniqueDynamic(this, &ThisClass::HandleGameTimeChanged);
	StoreGameState->OnGameClockRunningChanged.AddUniqueDynamic(this, &ThisClass::HandleGameClockRunningChanged);
	StoreGameState->OnFundsChanged.AddUniqueDynamic(this, &ThisClass::HandleFundsChanged);
	StoreGameState->OnReputationChanged.AddUniqueDynamic(this, &ThisClass::HandleReputationChanged);
	StoreGameState->OnReputationGradeChanged.AddUniqueDynamic(this, &ThisClass::HandleReputationGradeChanged);
	StoreGameState->OnBusinessDayStarted.AddUniqueDynamic(this, &ThisClass::HandleBusinessDayStarted);
	StoreGameState->OnStoreExperienceChanged.AddUniqueDynamic(this, &ThisClass::HandleStoreExperienceChanged);
	StoreGameState->OnStoreLevelChanged.AddUniqueDynamic(this, &ThisClass::HandleStoreLevelChanged);

	// 생성 시에는 현재 상태만 맞추고 확장 연출은 재생하지 않습니다.
	RefreshStatus(false);
	HandleGameTimeChanged(StoreGameState->CurrentGameMinutes);
	HandleGameClockRunningChanged(StoreGameState->bIsGameClockRunning);
	HandleFundsChanged(StoreGameState->GetCurrentFunds());
	HandleReputationChanged(StoreGameState->GetReputationScore());
	HandleReputationGradeChanged(StoreGameState->GetReputationGrade());
	HandleBusinessDayStarted(StoreGameState->CurrentBusinessDay);
	UpdateStoreProgressDisplay();
}

void UCSStoreTopBarWidget::NativeDestruct()
{
	GetWorld()->GetTimerManager().ClearTimer(StatusDetailTimerHandle);
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnStorePhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhaseChanged);
		StoreGameState->OnStoreOpenStateChanged.RemoveDynamic(this, &ThisClass::HandleStoreOpenStateChanged);
		StoreGameState->OnGameTimeChanged.RemoveDynamic(this, &ThisClass::HandleGameTimeChanged);
		StoreGameState->OnGameClockRunningChanged.RemoveDynamic(this, &ThisClass::HandleGameClockRunningChanged);
		StoreGameState->OnFundsChanged.RemoveDynamic(this, &ThisClass::HandleFundsChanged);
		StoreGameState->OnReputationChanged.RemoveDynamic(this, &ThisClass::HandleReputationChanged);
		StoreGameState->OnReputationGradeChanged.RemoveDynamic(this, &ThisClass::HandleReputationGradeChanged);
		StoreGameState->OnBusinessDayStarted.RemoveDynamic(this, &ThisClass::HandleBusinessDayStarted);
		StoreGameState->OnStoreExperienceChanged.RemoveDynamic(this, &ThisClass::HandleStoreExperienceChanged);
		StoreGameState->OnStoreLevelChanged.RemoveDynamic(this, &ThisClass::HandleStoreLevelChanged);
	}
	Super::NativeDestruct();
}

void UCSStoreTopBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	CurrentStatusWidth = FMath::FInterpTo(CurrentStatusWidth, TargetStatusWidth, InDeltaTime, WidthBlendSpeed);
	if (IsValid(StatusSizeBox)) StatusSizeBox->SetWidthOverride(CurrentStatusWidth);
	const float ColorAlpha = FMath::Clamp(InDeltaTime * ColorBlendSpeed, 0.0f, 1.0f);
	if (IsValid(StatusBackgroundImage))
	{
		StatusBackgroundImage->SetColorAndOpacity(FMath::Lerp(
			StatusBackgroundImage->GetColorAndOpacity(), TargetStatusColor, ColorAlpha));
	}
	if (IsValid(StatusIconImage))
	{
		StatusIconImage->SetColorAndOpacity(FMath::Lerp(
			StatusIconImage->GetColorAndOpacity(), TargetStatusColor, ColorAlpha));
	}
	if (IsValid(StatusText))
	{
		StatusText->SetColorAndOpacity(FMath::Lerp(
			StatusText->GetColorAndOpacity().GetSpecifiedColor(), TargetStatusColor, ColorAlpha));
		if (bTextFadingOut)
		{
			const float Opacity = FMath::FInterpTo(StatusText->GetRenderOpacity(), 0.0f, InDeltaTime, TextFadeSpeed);
			StatusText->SetRenderOpacity(Opacity);
			if (Opacity <= 0.05f)
			{
				StatusText->SetText(PendingStatusText);
				bTextFadingOut = false;
				bTextFadingIn = true;
			}
		}
		else if (bTextFadingIn)
		{
			const float Opacity = FMath::FInterpTo(StatusText->GetRenderOpacity(), 1.0f, InDeltaTime, TextFadeSpeed);
			StatusText->SetRenderOpacity(Opacity);
			if (Opacity >= 0.95f)
			{
				StatusText->SetRenderOpacity(1.0f);
				bTextFadingIn = false;
			}
		}
	}
}

void UCSStoreTopBarWidget::HandlePhaseChanged(ECStorePhase NewPhase)
{
	(void)NewPhase;
	RefreshStatus(true);
}

void UCSStoreTopBarWidget::HandleStoreOpenStateChanged(bool bNewStoreOpen)
{
	(void)bNewStoreOpen;
	// 개점 시 두 이벤트가 이어서 오므로 실제 표시 상태가 달라졌을 때만 연출합니다.
	RefreshStatus(true);
}

void UCSStoreTopBarWidget::RefreshStatus(bool bAllowAnimation)
{
	if (!IsValid(StoreGameState)) return;

	ECSStoreTopBarStatus NewStatus = ECSStoreTopBarStatus::Closed;
	if (StoreGameState->CurrentPhase == ECStorePhase::Preparation ||
		(StoreGameState->CurrentPhase == ECStorePhase::Open && !StoreGameState->bHasStoreOpenedToday))
	{
		NewStatus = ECSStoreTopBarStatus::Preparation;
	}
	else if (StoreGameState->CurrentPhase == ECStorePhase::Open)
	{
		NewStatus = StoreGameState->bIsStoreOpen
			? ECSStoreTopBarStatus::OpenEntryAllowed
			: ECSStoreTopBarStatus::OpenEntryPaused;
	}

	if (bStatusInitialized && DisplayedStatus == NewStatus) return;
	const bool bAnimate = bStatusInitialized && bAllowAnimation;
	const bool bExpand = bAnimate &&
		(NewStatus == ECSStoreTopBarStatus::OpenEntryAllowed ||
		 NewStatus == ECSStoreTopBarStatus::OpenEntryPaused);
	DisplayedStatus = NewStatus;
	bStatusInitialized = true;

	const FText ShortText = NewStatus == ECSStoreTopBarStatus::Preparation
		? NSLOCTEXT("StoreTopBar", "Preparation", "영업 전")
		: NewStatus == ECSStoreTopBarStatus::Closed
			? NSLOCTEXT("StoreTopBar", "Closed", "영업 종료")
			: NSLOCTEXT("StoreTopBar", "Open", "영업 중");
	const FText DetailText = NewStatus == ECSStoreTopBarStatus::OpenEntryAllowed
		? NSLOCTEXT("StoreTopBar", "EntryAllowed", "영업 중 - 입장 가능")
		: NewStatus == ECSStoreTopBarStatus::OpenEntryPaused
			? NSLOCTEXT("StoreTopBar", "EntryPaused", "영업 중 - 잠시 휴점")
			: ShortText;
	ShortStatusText = ShortText;
	TargetStatusColor = NewStatus == ECSStoreTopBarStatus::Preparation ? PreparationColor
		: NewStatus == ECSStoreTopBarStatus::OpenEntryAllowed ? OpenAllowedColor
		: NewStatus == ECSStoreTopBarStatus::OpenEntryPaused ? OpenPausedColor : ClosedColor;
	GetWorld()->GetTimerManager().ClearTimer(StatusDetailTimerHandle);
	TargetStatusWidth = bExpand ? ExpandedStatusWidth : ShortStatusWidth;
	SetStatusTextWithBlend(bExpand ? DetailText : ShortText, bAnimate);
	if (!bAnimate)
	{
		CurrentStatusWidth = TargetStatusWidth;
		if (IsValid(StatusSizeBox)) StatusSizeBox->SetWidthOverride(CurrentStatusWidth);
		ApplyStatusColor(TargetStatusColor);
	}
	if (bExpand)
	{
		GetWorld()->GetTimerManager().SetTimer(StatusDetailTimerHandle,
			this, &ThisClass::CollapseStatusBanner, StatusDetailSeconds, false);
	}
	OnStoreStatusVisualChanged(NewStatus, bAnimate, bExpand);
}

void UCSStoreTopBarWidget::SetStatusTextWithBlend(const FText& NewText, bool bAnimate)
{
	if (!IsValid(StatusText)) return;
	PendingStatusText = NewText;
	if (!bAnimate)
	{
		StatusText->SetText(NewText);
		StatusText->SetRenderOpacity(1.0f);
		bTextFadingOut = false;
		bTextFadingIn = false;
		return;
	}
	// 한 텍스트 블록을 쓰므로 문구를 낮은 불투명도에서 교체합니다.
	bTextFadingOut = true;
	bTextFadingIn = false;
}

void UCSStoreTopBarWidget::ApplyStatusColor(const FLinearColor& NewColor)
{
	if (IsValid(StatusBackgroundImage)) StatusBackgroundImage->SetColorAndOpacity(NewColor);
	if (IsValid(StatusIconImage)) StatusIconImage->SetColorAndOpacity(NewColor);
	if (IsValid(StatusText)) StatusText->SetColorAndOpacity(NewColor);
}

void UCSStoreTopBarWidget::CollapseStatusBanner()
{
	TargetStatusWidth = ShortStatusWidth;
	SetStatusTextWithBlend(ShortStatusText, true);
	OnStoreStatusDetailFinished(DisplayedStatus);
}

void UCSStoreTopBarWidget::HandleGameTimeChanged(float NewGameMinutes)
{
	if (!IsValid(StoreGameState)) return;
	const float CountdownDuration = FMath::Max(1.0f, StoreGameState->ClosingClockCountdownMinutes);
	const float CountdownStart = StoreGameState->AutomaticClosingTimeMinutes - CountdownDuration;
	const bool bClosingCountdown = NewGameMinutes >= CountdownStart &&
		NewGameMinutes <= StoreGameState->AutomaticClosingTimeMinutes;
	const int32 WholeMinutes = FMath::FloorToInt(FMath::Max(0.0f, NewGameMinutes));
	const int32 DisplayMinutes = bClosingCountdown ? WholeMinutes : (WholeMinutes / 10) * 10;
	const int32 WrappedMinutes = DisplayMinutes % 1440;
	const int32 Hour = WrappedMinutes / 60;
	const int32 Minute = WrappedMinutes % 60;
	if (IsValid(ClockHourText)) ClockHourText->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Hour)));
	if (IsValid(ClockColonText)) ClockColonText->SetText(FText::FromString(TEXT(":")));
	if (IsValid(ClockMinuteText)) ClockMinuteText->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Minute)));
	if (IsValid(ClockText)) ClockText->SetText(FText::FromString(FString::Printf(TEXT("%02d:%02d"), Hour, Minute)));
	const int32 Elapsed = bClosingCountdown
		? FMath::Clamp(WholeMinutes - FMath::CeilToInt(CountdownStart), 0, FMath::CeilToInt(CountdownDuration))
		: 0;
	ApplyClosingClockColor(bClosingCountdown ? FMath::Clamp(static_cast<float>(Elapsed) / CountdownDuration, 0.0f, 1.0f) : 0.0f);
}

void UCSStoreTopBarWidget::ApplyClosingClockColor(float Alpha)
{
	if (IsValid(ClockText)) ClockText->SetColorAndOpacity(FMath::Lerp(InitialClockColor, ClosingClockColor, Alpha));
	if (IsValid(ClockHourText)) ClockHourText->SetColorAndOpacity(FMath::Lerp(InitialClockHourColor, ClosingClockColor, Alpha));
	if (IsValid(ClockColonText)) ClockColonText->SetColorAndOpacity(FMath::Lerp(InitialClockColonColor, ClosingClockColor, Alpha));
	if (IsValid(ClockMinuteText)) ClockMinuteText->SetColorAndOpacity(FMath::Lerp(InitialClockMinuteColor, ClosingClockColor, Alpha));
}

void UCSStoreTopBarWidget::HandleGameClockRunningChanged(bool bNewClockRunning)
{
	OnClockTickStateChanged(bNewClockRunning);
	if (!bNewClockRunning && IsValid(ClockColonText)) ClockColonText->SetRenderOpacity(1.0f);
}

void UCSStoreTopBarWidget::HandleFundsChanged(int32 NewFunds)
{
	if (IsValid(FundsText)) FundsText->SetText(FText::AsNumber(NewFunds));
}

void UCSStoreTopBarWidget::HandleReputationChanged(int32 NewReputationScore)
{
	if (IsValid(ReputationScoreText)) ReputationScoreText->SetText(FText::AsNumber(NewReputationScore));
}

void UCSStoreTopBarWidget::HandleReputationGradeChanged(ECSReputationGrade NewGrade)
{
	if (IsValid(ReputationGradeText)) ReputationGradeText->SetText(GetReputationGradeDisplayName(NewGrade));
}

void UCSStoreTopBarWidget::HandleBusinessDayStarted(int32 NewBusinessDay)
{
	if (IsValid(BusinessDayText)) BusinessDayText->SetText(FText::Format(NSLOCTEXT("StoreHUD", "BusinessDayFormat", "{0} 일차"), FText::AsNumber(NewBusinessDay)));
}

void UCSStoreTopBarWidget::HandleStoreExperienceChanged(int32 NewExperience)
{
	(void)NewExperience;
	UpdateStoreProgressDisplay();
}

void UCSStoreTopBarWidget::HandleStoreLevelChanged(int32 NewLevel)
{
	(void)NewLevel;
	UpdateStoreProgressDisplay();
}

void UCSStoreTopBarWidget::UpdateStoreProgressDisplay()
{
	if (!IsValid(StoreGameState)) return;
	if (IsValid(StoreLevelText)) StoreLevelText->SetText(FText::AsNumber(StoreGameState->GetStoreLevel()));
	if (IsValid(StoreLevelNameText)) StoreLevelNameText->SetText(StoreGameState->GetStoreLevelDisplayName());
	if (IsValid(StoreExperienceProgressBar)) StoreExperienceProgressBar->SetPercent(StoreGameState->GetStoreLevelProgress());
	if (!IsValid(StoreExperienceText)) return;
	StoreExperienceText->SetText(StoreGameState->IsMaximumStoreLevel()
		? NSLOCTEXT("StoreHUD", "MaximumStoreLevel", "MAX LEVEL")
		: FText::Format(NSLOCTEXT("StoreHUD", "StoreExperienceFormat", "{0} / {1} EXP"),
			FText::AsNumber(StoreGameState->GetStoreExperienceInCurrentLevel()),
			FText::AsNumber(StoreGameState->GetStoreExperienceRequiredForNextLevel())));
}

FText UCSStoreTopBarWidget::GetReputationGradeDisplayName(ECSReputationGrade Grade) const
{
	switch (Grade)
	{
	case ECSReputationGrade::VeryBad: return NSLOCTEXT("StoreHUD", "ReputationVeryBad", "Very Bad");
	case ECSReputationGrade::Bad: return NSLOCTEXT("StoreHUD", "ReputationBad", "Bad");
	case ECSReputationGrade::Normal: return NSLOCTEXT("StoreHUD", "ReputationNormal", "Normal");
	case ECSReputationGrade::Good: return NSLOCTEXT("StoreHUD", "ReputationGood", "Good");
	case ECSReputationGrade::Excellent: return NSLOCTEXT("StoreHUD", "ReputationExcellent", "Excellent");
	default: return FText::GetEmpty();
	}
}
