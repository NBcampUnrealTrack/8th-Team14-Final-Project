#include "Systems/StoreFlow/CSStoreGameState.h"

void ACSStoreGameState::SetStorePhase(const FCSStorePhaseData& PhaseData)
{
	CurrentPhase = PhaseData.Phase;
	CurrentPhaseDisplayName = PhaseData.DisplayName;
	bCanRestock = PhaseData.bCanRestock;
	bCanCheckout = PhaseData.bCanCheckout;
	bCanSpawnCustomers = PhaseData.bCanSpawnCustomers;

	SetRemainingSeconds(0.0f);
	OnStorePhaseChanged.Broadcast(CurrentPhase);
}

void ACSStoreGameState::SetGameClock(
	float NewGameMinutes,
	float NewGameMinutesPerRealSecond,
	bool bNewClockRunning)
{
	GameMinutesPerRealSecond = FMath::Max(0.0f, NewGameMinutesPerRealSecond);
	bIsGameClockRunning = bNewClockRunning;
	SetCurrentGameMinutes(NewGameMinutes);
}

void ACSStoreGameState::SetCurrentGameMinutes(float NewGameMinutes)
{
	CurrentGameMinutes = FMath::Clamp(NewGameMinutes, 0.0f, 1440.0f);
	OnGameTimeChanged.Broadcast(CurrentGameMinutes);
}

void ACSStoreGameState::SetEndOfDayTask(ECEndOfDayTask NewTask)
{
	CurrentEndOfDayTask = NewTask;
	OnEndOfDayTaskChanged.Broadcast(CurrentEndOfDayTask);
}

void ACSStoreGameState::SetRemainingSeconds(float NewRemainingSeconds)
{
	RemainingSeconds = FMath::Max(0.0f, NewRemainingSeconds);
	OnRemainingTimeChanged.Broadcast(RemainingSeconds);
}
