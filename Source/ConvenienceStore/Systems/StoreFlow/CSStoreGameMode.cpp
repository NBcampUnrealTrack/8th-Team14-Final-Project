#include "Systems/StoreFlow/CSStoreGameMode.h"

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
			TEXT("[StoreGameMode] ACSStoreGameState를 찾을 수 없어 상점 성장 상태를 초기화하지 못했습니다."));
	}
	else if (!IsValid(StoreProgressionDataAsset))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] StoreProgressionDataAsset이 지정되지 않아 기본 성장 상태만 유지합니다."));
	}
	else if (!StoreGameState->InitializeStoreProgression(StoreProgressionDataAsset))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameMode] StoreProgressionDataAsset으로 성장 상태를 초기화하지 못했습니다."));
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
	StoreGameState->SetGameClock(
		InitialGameTimeMinutes,
		GameMinutesPerRealSecond,
		true);

	UpdateStorePhaseForCurrentTime();
	CheckScheduleEvents(InitialGameTimeMinutes - 0.01f, InitialGameTimeMinutes);

	GetWorldTimerManager().ClearTimer(GameClockTimerHandle);
	GetWorldTimerManager().SetTimer(
		GameClockTimerHandle,
		this,
		&ACSStoreGameMode::UpdateGameClock,
		0.1f,
		true);
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

	if (NewGameMinutes >= 1440.0f)
	{
		StoreGameState->SetCurrentGameMinutes(1440.0f);
		BeginClosingCheckpoint();
		return;
	}

	StoreGameState->SetCurrentGameMinutes(NewGameMinutes);
	CheckScheduleEvents(PreviousGameMinutes, NewGameMinutes);
	UpdateStorePhaseForCurrentTime();
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

	FCSStorePhaseData ClosingPhase;
	ClosingPhase.Phase = ECStorePhase::Settlement;
	ClosingPhase.DisplayName = NSLOCTEXT("Store", "ClosingTasks", "Closing Tasks");
	StoreGameState->SetStorePhase(ClosingPhase);
	StoreGameState->SetEndOfDayTask(ECEndOfDayTask::OrderReview);
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
