#include "Systems/StoreFlow/CSStoreGameMode.h"

#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSStoreProgressionDataAsset.h"
#include "Engine/DataTable.h"
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
	StoreGameState->SetEndOfDayTask(ECEndOfDayTask::None);
	StoreGameState->CompleteDailySummary();
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
	StoreGameState->SetStorePhase(MakePhaseData(
		ECStorePhase::ClosingFreeRoam, NSLOCTEXT("Store", "ClosingFreeRoam", "마감 활동")));
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
	StoreGameState->bIsGameClockRunning = false;
	// 새벽 02:00에는 입간판 상태와 무관하게 영업을 강제 종료합니다.
	StoreGameState->bIsStoreOpen = false;
	StoreGameState->OnStoreOpenStateChanged.Broadcast(false);

	// 통계 확인 전에는 UI만 표시하고, 확인 후에 마감 자유 활동 단계로 전환합니다.
	StoreGameState->RequestDailySummary();
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
