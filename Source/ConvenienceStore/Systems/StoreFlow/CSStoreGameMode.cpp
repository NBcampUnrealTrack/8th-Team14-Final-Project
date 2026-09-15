#include "Systems/StoreFlow/CSStoreGameMode.h"

#include "Data/CSAudioConfigDataAsset.h"
#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSStoreEconomyDataAsset.h"
#include "Data/CSStoreProgressionDataAsset.h"
#include "Data/CSWeatherSettingsDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

ACSStoreGameMode::ACSStoreGameMode()
{
	GameStateClass = ACSStoreGameState::StaticClass();
	PlayerControllerClass = ACSStorePlayerController::StaticClass();
}

void ACSStoreGameMode::BeginPlay()
{
	Super::BeginPlay();
	StoreGameState = GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] ACSStoreGameState를 찾을 수 없어 상점 설정을 초기화하지 못했습니다."));
	}
	else
	{
		if (!IsValid(StoreProgressionDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreProgressionDataAsset이 지정되지 않아 기본 성장 상태만 유지합니다."));
		}
		else if (!StoreGameState->InitializeStoreProgression(StoreProgressionDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreProgressionDataAsset으로 성장 상태를 초기화하지 못했습니다."));
		}

		if (!IsValid(FreshnessSettingsDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] FreshnessSettingsDataAsset이 지정되지 않아 신선도 감소를 적용하지 않습니다."));
		}
		else if (!StoreGameState->InitializeFreshnessSettings(FreshnessSettingsDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] FreshnessSettingsDataAsset으로 신선도 설정을 초기화하지 못했습니다."));
		}

		if (!IsValid(StoreEconomyDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreEconomyDataAsset이 지정되지 않아 임대료를 처리하지 않습니다."));
		}
		else if (!StoreGameState->InitializeStoreEconomy(StoreEconomyDataAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] StoreEconomyDataAsset 설정이 유효하지 않아 임대료를 처리하지 않습니다."));
		}
	}

	if (!IsValid(StoreAudioConfigDataAsset))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] StoreAudioConfigDataAsset이 지정되지 않아 공용 사운드 시스템을 초기화하지 않습니다."));
	}
	else if (UGameInstance* GameInstance = GetGameInstance(); IsValid(GameInstance))
	{
		if (UCSAudioSubsystem* AudioSubsystem =
			GameInstance->GetSubsystem<UCSAudioSubsystem>(); IsValid(AudioSubsystem))
		{
			if (!AudioSubsystem->InitializeAudio(StoreAudioConfigDataAsset))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[StoreGameMode] StoreAudioConfigDataAsset으로 공용 사운드 시스템을 초기화하지 못했습니다."));
			}
			else if (!AudioSubsystem->PlayBGM())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[StoreGameMode] StoreBGM이 지정되지 않았거나 현재 월드에서 재생할 수 없습니다."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameMode] UCSAudioSubsystem을 찾을 수 없습니다."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] GameInstance를 찾을 수 없어 공용 사운드 시스템을 초기화하지 못했습니다."));
	}

	if (bStartFlowOnBeginPlay)
	{
		StartStoreFlow();
	}
}

void ACSStoreGameMode::StartStoreFlow()
{
	if (StoreGameState == nullptr)
	{
		StoreGameState = GetGameState<ACSStoreGameState>();
	}

	if (StoreGameState == nullptr)
	{
		return;
	}

	FiredScheduleEventRows.Empty();
	bClosingWarningFired = false;
	StoreGameState->SetEndOfDayTask(ECEndOfDayTask::None);
	StoreGameState->CompleteDailySummary();
	StoreGameState->SetClosingTimeSettings(
		AutomaticClosingTimeMinutes,
		ClosingClockCountdownMinutes);
	StoreGameState->SetGameClock(
		InitialGameTimeMinutes,
		GameMinutesPerRealSecond,
		false);

	StoreGameState->SetStorePhase(MakePhaseData(
		ECStorePhase::Preparation, NSLOCTEXT("Store", "Preparation", "준비")));
	// 영업 시간대가 되어도 입간판 확인 전까지는 고객을 받지 않습니다.
	StoreGameState->bIsStoreOpen = false;
	StoreGameState->OnStoreOpenStateChanged.Broadcast(false);
	GetWorldTimerManager().ClearTimer(GameClockTimerHandle);
	GetWorldTimerManager().SetTimer(
		GameClockTimerHandle,
		this,
		&ACSStoreGameMode::UpdateGameClock,
		0.1f,
		true);
}

bool ACSStoreGameMode::RequestBusinessOpenState(bool bShouldOpen)
{
	if (!IsValid(StoreGameState) || !StoreGameState->CanChangeStoreOpenState())
	{
		return false;
	}

	if (StoreGameState->CurrentPhase == ECStorePhase::Preparation)
	{
		if (!bShouldOpen)
		{
			return false;
		}

		StoreGameState->SetGameClock(BusinessStartTimeMinutes, GameMinutesPerRealSecond, true);
		StoreGameState->SetStorePhase(MakePhaseData(
			ECStorePhase::Open, NSLOCTEXT("Store", "Business", "영업")));
		CheckScheduleEvents(BusinessStartTimeMinutes - 0.01f, BusinessStartTimeMinutes);
	}

	return StoreGameState->SetStoreOpen(bShouldOpen);
}

void ACSStoreGameMode::CompleteDailySummary()
{
	if (!IsValid(StoreGameState) || !StoreGameState->bIsDailySummaryPending)
	{
		return;
	}

	StoreGameState->CompleteDailySummary();
	StartNextBusinessDay();
}

bool ACSStoreGameMode::RequestClosingDailySummary()
{
	if (StoreGameState == nullptr)
	{
		StoreGameState = GetGameState<ACSStoreGameState>();
	}

	if (!IsValid(StoreGameState) ||
		StoreGameState->CurrentPhase != ECStorePhase::ClosingFreeRoam ||
		StoreGameState->bIsDailySummaryPending)
	{
		return false;
	}

	// 정산 화면을 띄우기 전에 남아 있는 손님을 전부 정리합니다.
	// DespawnAllCustomers가 절도범과 일반 손님을 두 갈래로 나눠 처리하므로
	// 이 호출 하나로 둘 다 마감 종 시점에 확정됩니다.
	//   절도범   : 상품을 유지한 채 파괴 → 손실이 당일 장부로 확정
	//   그 외    : 상품을 낙하시켜 재고로 회수한 뒤 파괴
	//
	// RequestDailySummary가 요약 위젯을 생성하므로 반드시 그 앞에서 불러야
	// 위젯 첫 프레임부터 올바른 금액이 보입니다. 뒤에 두면 플레이어 눈앞에서 금액이 튀어오릅니다.
	if (UCSCustomerRuntimeSubsystem* CustomerRuntimeSubsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr)
	{
		CustomerRuntimeSubsystem->DespawnAllCustomers();
	}

	StoreGameState->RequestDailySummary();
	return true;
}

void ACSStoreGameMode::StartPhase(ECStorePhase NewPhase)
{
	if (StoreGameState == nullptr)
	{
		StoreGameState = GetGameState<ACSStoreGameState>();
	}

	if (StoreGameState == nullptr)
	{
		return;
	}

	FCSStorePhaseData FinishedPhaseData;
	const FCSStorePhaseData* PhaseData = FindPhaseData(NewPhase);
	if (PhaseData == nullptr && NewPhase == ECStorePhase::Finished)
	{
		FinishedPhaseData.Phase = ECStorePhase::Finished;
		FinishedPhaseData.DisplayName = NSLOCTEXT(
			"Store", "FinishedPhase", "Finished");
		PhaseData = &FinishedPhaseData;
	}

	if (PhaseData == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Store phase could not start. Assign DT_StorePhase to StorePhaseDataTable."));
		return;
	}

	StoreGameState->SetStorePhase(*PhaseData);
}

void ACSStoreGameMode::UpdateGameClock()
{
	if (StoreGameState == nullptr || !StoreGameState->bIsGameClockRunning)
	{
		return;
	}

	const float PreviousGameMinutes = StoreGameState->CurrentGameMinutes;
	const float NewGameMinutes = PreviousGameMinutes +
		StoreGameState->GameMinutesPerRealSecond * 0.1f;
	const float ClosingWarningTime =
		AutomaticClosingTimeMinutes - ClosingWarningLeadMinutes;
	if (!bClosingWarningFired && ClosingWarningLeadMinutes > 0.0f &&
		PreviousGameMinutes < ClosingWarningTime &&
		NewGameMinutes >= ClosingWarningTime)
	{
		bClosingWarningFired = true;
		StoreGameState->OnClosingWarningRequested.Broadcast(
			FMath::RoundToInt(ClosingWarningLeadMinutes));
	}

	if (NewGameMinutes >= AutomaticClosingTimeMinutes)
	{
		StoreGameState->SetCurrentGameMinutes(AutomaticClosingTimeMinutes);
		BeginClosingCheckpoint();
		return;
	}

	StoreGameState->SetCurrentGameMinutes(NewGameMinutes);
	CheckScheduleEvents(PreviousGameMinutes, NewGameMinutes);
}

void ACSStoreGameMode::UpdateStorePhaseForCurrentTime()
{
	const FCSStorePhaseData* PhaseData = FindPhaseDataAtTime(
		StoreGameState->CurrentGameMinutes);

	if (PhaseData != nullptr &&
		(StoreGameState->CurrentPhase != PhaseData->Phase ||
			StoreGameState->CurrentPhaseDisplayName.IsEmpty()))
	{
		StoreGameState->SetStorePhase(*PhaseData);
	}
}

void ACSStoreGameMode::CheckScheduleEvents(
	float PreviousGameMinutes,
	float NewGameMinutes)
{
	if (StoreScheduleEventDataTable == nullptr || StoreGameState == nullptr)
	{
		return;
	}

	for (const FName& RowName : StoreScheduleEventDataTable->GetRowNames())
	{
		if (FiredScheduleEventRows.Contains(RowName))
		{
			continue;
		}

		const FCSStoreScheduleEventData* Row =
			StoreScheduleEventDataTable->FindRow<FCSStoreScheduleEventData>(
				RowName, TEXT("CheckStoreSchedule"));

		if (Row != nullptr && PreviousGameMinutes < Row->TriggerTimeMinutes &&
			NewGameMinutes >= Row->TriggerTimeMinutes)
		{
			FiredScheduleEventRows.Add(RowName);
			StoreGameState->OnScheduleEventFired.Broadcast(RowName, Row->Message);
		}
	}
}

void ACSStoreGameMode::BeginClosingCheckpoint()
{
	GetWorldTimerManager().ClearTimer(GameClockTimerHandle);
	// 시계 정지 상태를 HUD에도 알리기 위해 setter로 변경합니다.
	// StoreGameState->bIsGameClockRunning = false;
	StoreGameState->SetGameClockRunning(false);
	// 새벽 02:00에는 입간판 상태와 무관하게 영업을 강제 종료합니다.
	StoreGameState->bIsStoreOpen = false;
	StoreGameState->OnStoreOpenStateChanged.Broadcast(false);

	// 마감 자유 활동에 들어가기 전에 오늘 임대료를 실제 지출로 확정합니다.
	StoreGameState->ProcessDailyRent();
	StoreGameState->ProcessDailyEmployeeWages();

	// 통계는 자동으로 열지 않고 플레이어가 마감종을 확인할 때 요청합니다.
	StoreGameState->SetStorePhase(MakePhaseData(
		ECStorePhase::ClosingFreeRoam,
		NSLOCTEXT("Store", "ClosingFreeRoam", "마감 활동")));
}

void ACSStoreGameMode::CompleteCurrentEndOfDayTask()
{
	if (StoreGameState == nullptr ||
		StoreGameState->CurrentEndOfDayTask == ECEndOfDayTask::None)
	{
		return;
	}

	const ECEndOfDayTask NextTask = GetNextEndOfDayTask(
		StoreGameState->CurrentEndOfDayTask);
	StoreGameState->SetEndOfDayTask(NextTask);

	if (NextTask == ECEndOfDayTask::None)
	{
		// 마감 업무가 모두 끝나면 다음 날 02:00 AM 상태로 이동합니다.
		StoreGameState->SetCurrentGameMinutes(120.0f);

		FCSStorePhaseData NextDayPhase;
		NextDayPhase.Phase = ECStorePhase::Finished;
		NextDayPhase.DisplayName = NSLOCTEXT("Store", "NextDay", "Next Day");
		StoreGameState->SetStorePhase(NextDayPhase);
	}
}

void ACSStoreGameMode::StartNextBusinessDay()
{
	if (StoreGameState != nullptr)
	{
		// 다음 날이 시작되기 전에 전날의 판매 요약값을 초기화합니다.
		StoreGameState->BeginNextBusinessDay();
	}

	StartStoreFlow();
}

ECEndOfDayTask ACSStoreGameMode::GetNextEndOfDayTask(ECEndOfDayTask Task) const
{
	switch (Task)
	{
	case ECEndOfDayTask::OrderReview:
		return ECEndOfDayTask::Settlement;
	case ECEndOfDayTask::Settlement:
		return ECEndOfDayTask::SalesReport;
	case ECEndOfDayTask::SalesReport:
		return ECEndOfDayTask::ReputationUpdate;
	case ECEndOfDayTask::ReputationUpdate:
	default:
		return ECEndOfDayTask::None;
	}
}

FCSStorePhaseData ACSStoreGameMode::MakePhaseData(
	ECStorePhase Phase, const FText& DisplayName) const
{
	FCSStorePhaseData PhaseData;
	PhaseData.Phase = Phase;
	PhaseData.DisplayName = DisplayName;
	PhaseData.bCanRestock = Phase != ECStorePhase::Open;
	PhaseData.bCanCheckout = Phase == ECStorePhase::Open;
	PhaseData.bCanSpawnCustomers = Phase == ECStorePhase::Open;
	return PhaseData;
}

const FCSStorePhaseData* ACSStoreGameMode::FindPhaseData(ECStorePhase Phase) const
{
	if (StorePhaseDataTable == nullptr)
	{
		return nullptr;
	}

	for (const FName& RowName : StorePhaseDataTable->GetRowNames())
	{
		if (const FCSStorePhaseData* Row = StorePhaseDataTable->FindRow<FCSStorePhaseData>(
			RowName, TEXT("FindStorePhase"));
			Row != nullptr && Row->Phase == Phase)
		{
			return Row;
		}
	}

	return nullptr;
}

const FCSStorePhaseData* ACSStoreGameMode::FindPhaseDataAtTime(float GameMinutes) const
{
	if (StorePhaseDataTable == nullptr)
	{
		return nullptr;
	}

	for (const FName& RowName : StorePhaseDataTable->GetRowNames())
	{
		const FCSStorePhaseData* Row = StorePhaseDataTable->FindRow<FCSStorePhaseData>(
			RowName, TEXT("FindStorePhaseAtTime"));

		if (Row != nullptr && GameMinutes >= Row->StartTimeMinutes &&
			GameMinutes < Row->EndTimeMinutes)
		{
			return Row;
		}
	}

	return nullptr;
}

bool ACSStoreGameMode::IsTutorialEnabled() const
{
	return bTutorialEnabled;
}

UDataTable* ACSStoreGameMode::GetTutorialStepDataTable() const
{
	return TutorialStepDataTable;
}

TSubclassOf<UCSTutorialBannerWidgetBase> ACSStoreGameMode::GetTutorialBannerWidgetClass() const
{
	return TutorialBannerWidgetClass;
}

TSubclassOf<UCSTutorialDialogueWidgetBase> ACSStoreGameMode::GetTutorialDialogueWidgetClass() const
{
	return TutorialDialogueWidgetClass;
}

TSubclassOf<UCSTutorialObjectiveWidgetBase> ACSStoreGameMode::GetTutorialObjectiveWidgetClass() const
{
	return TutorialObjectiveWidgetClass;
}

TSubclassOf<AActor> ACSStoreGameMode::GetTutorialShelfFacilityClass() const
{
	return TutorialShelfFacilityClass;
}

UCSWeatherSettingsDataAsset* ACSStoreGameMode::GetWeatherSettings() const
{
	return WeatherSettingsDataAsset;
}
