#include "Systems/StoreFlow/CSStoreGameState.h"

#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSStoreEconomyDataAsset.h"
#include "Data/CSStoreProgressionDataAsset.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Systems/Employees/CSEmployeeHiringSubsystem.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Engine/World.h"

int32 ACSStoreGameState::GetMagicCircleUpgradeCost() const
{
	if (MagicCircleLevel >= 5) return 0;
	const UCSStoreEconomyDataAsset* Settings = IsValid(StoreEconomyDataAsset)
		? StoreEconomyDataAsset.Get() : GetDefault<UCSStoreEconomyDataAsset>();
	const int32 Index = MagicCircleLevel - 1;
	return Settings->MagicCircleUpgradeCosts.IsValidIndex(Index)
		? FMath::Max(1, Settings->MagicCircleUpgradeCosts[Index])
		: GetDefault<UCSStoreEconomyDataAsset>()->MagicCircleUpgradeCosts[Index];
}

int32 ACSStoreGameState::GetMagicCircleRequiredStoreLevel() const
{
	const UCSStoreEconomyDataAsset* Settings = IsValid(StoreEconomyDataAsset)
		? StoreEconomyDataAsset.Get() : GetDefault<UCSStoreEconomyDataAsset>();
	const int32 Index = MagicCircleLevel - 1;
	return FMath::Clamp(Settings->MagicCircleRequiredStoreLevels.IsValidIndex(Index)
		? Settings->MagicCircleRequiredStoreLevels[Index] : 10, 1, 10);
}

int32 ACSStoreGameState::GetEmployeeRequiredStoreLevel(ECSEmployeeHireSlot Slot) const
{
	const UCSStoreEconomyDataAsset* Settings = IsValid(StoreEconomyDataAsset)
		? StoreEconomyDataAsset.Get() : GetDefault<UCSStoreEconomyDataAsset>();
	switch (Slot)
	{
	case ECSEmployeeHireSlot::FirstRestock: return FMath::Clamp(Settings->FirstRestockRequiredStoreLevel, 1, 10);
	case ECSEmployeeHireSlot::Checkout: return FMath::Clamp(Settings->CheckoutRequiredStoreLevel, 1, 10);
	case ECSEmployeeHireSlot::SecondRestock: return FMath::Clamp(Settings->SecondRestockRequiredStoreLevel, 1, 10);
	default: return 10;
	}
}

bool ACSStoreGameState::CanUpgradeMagicCircle(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!HasAuthority())
		OutReason = NSLOCTEXT("MagicCircle", "Busy", "지금은 강화할 수 없습니다.");
	else if (MagicCircleLevel >= 5)
		OutReason = NSLOCTEXT("MagicCircle", "Maximum", "최대 단계 달성");
	else if (bIsDailySummaryPending || CurrentPhase == ECStorePhase::Settlement || CurrentPhase == ECStorePhase::Finished)
		OutReason = NSLOCTEXT("MagicCircle", "Settlement", "정산 중에는 강화할 수 없습니다.");
	else if (!IsStoreLevelRequirementMet(GetMagicCircleRequiredStoreLevel()))
		OutReason = FText::Format(NSLOCTEXT("MagicCircle", "StoreLevelRequired", "가게 레벨 {0}이 필요합니다. (현재 {1})"),
			FText::AsNumber(GetMagicCircleRequiredStoreLevel()), FText::AsNumber(GetStoreLevel()));
	else if (!CanAfford(GetMagicCircleUpgradeCost()))
		OutReason = NSLOCTEXT("MagicCircle", "InsufficientFunds", "골드가 부족합니다.");
	return OutReason.IsEmpty();
}

bool ACSStoreGameState::TryUpgradeMagicCircle(FText& OutMessage)
{
	if (bMagicCircleUpgradeInProgress)
	{
		OutMessage = NSLOCTEXT("MagicCircle", "Busy", "지금은 강화할 수 없습니다.");
		return false;
	}
	if (!CanUpgradeMagicCircle(OutMessage)) return false;
	TGuardValue<bool> UpgradeGuard(bMagicCircleUpgradeInProgress, true);
	const int32 Cost = GetMagicCircleUpgradeCost();
	if (static_cast<int64>(DailyMagicCircleUpgradeExpenseGold) + Cost > MAX_int32)
	{
		OutMessage = NSLOCTEXT("MagicCircle", "ExpenseOverflow", "강화 비용을 처리할 수 없습니다.");
		return false;
	}
	// 잔액 변경 이벤트에서도 강화된 단계와 지출 내역을 함께 읽도록 먼저 갱신합니다.
	++MagicCircleLevel;
	DailyMagicCircleUpgradeExpenseGold += Cost;
	if (!TrySpendFunds(Cost))
	{
		--MagicCircleLevel;
		DailyMagicCircleUpgradeExpenseGold -= Cost;
		OutMessage = NSLOCTEXT("MagicCircle", "PaymentFailed", "강화 비용 결제에 실패했습니다.");
		return false;
	}
	OutMessage = FText::Format(NSLOCTEXT("MagicCircle", "Upgraded", "마법진이 {0}단계로 강화되었습니다."), FText::AsNumber(MagicCircleLevel));
	OnMagicCircleUpgraded.Broadcast(MagicCircleLevel);
	return true;
}

int32 ACSStoreGameState::GetEmployeeHireCost(ECSEmployeeHireSlot Slot) const
{
	const UCSStoreEconomyDataAsset* Settings = IsValid(StoreEconomyDataAsset)
		? StoreEconomyDataAsset.Get() : GetDefault<UCSStoreEconomyDataAsset>();
	// 에디터 값이 잘못 입력되어도 가격 순서와 합계의 정수 범위를 지킵니다.
	const int32 First = FMath::Clamp(Settings->FirstRestockHireCost, 1, 100000000);
	const int32 Checkout = FMath::Clamp(Settings->CheckoutHireCost, First + 1, 100000001);
	const int32 Second = FMath::Clamp(Settings->SecondRestockHireCost, Checkout + 1, 100000002);
	switch (Slot)
	{
	case ECSEmployeeHireSlot::FirstRestock: return First;
	case ECSEmployeeHireSlot::Checkout: return Checkout;
	case ECSEmployeeHireSlot::SecondRestock: return Second;
	default: return 0;
	}
}

int32 ACSStoreGameState::GetEmployeeDailyWage(ECSEmployeeHireSlot Slot) const
{
	const UCSStoreEconomyDataAsset* Settings = IsValid(StoreEconomyDataAsset)
		? StoreEconomyDataAsset.Get() : GetDefault<UCSStoreEconomyDataAsset>();
	const double Percent = FMath::IsFinite(Settings->EmployeeDailyWagePercent)
		? FMath::Clamp(static_cast<double>(Settings->EmployeeDailyWagePercent), 1.0, 5.0) : 2.0;
	return FMath::CeilToInt(static_cast<double>(GetEmployeeHireCost(Slot)) * Percent / 100.0);
}

bool ACSStoreGameState::TryPayEmployeeHireCost(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0 || !CanAfford(Amount) ||
		static_cast<int64>(DailyEmployeeHireExpenseGold) + Amount > MAX_int32) return false;
	// 총지출은 TrySpendFunds만 변경하고 이 필드는 내역 분류에만 사용합니다.
	DailyEmployeeHireExpenseGold += Amount;
	if (!TrySpendFunds(Amount))
	{
		DailyEmployeeHireExpenseGold -= Amount;
		return false;
	}
	return true;
}

bool ACSStoreGameState::ProcessDailyEmployeeWages()
{
	if (!HasAuthority() || bPayrollInProgress) return false;
	if (LastPayrollProcessedBusinessDay == CurrentBusinessDay) return true;
	UCSEmployeeHiringSubsystem* Hiring = GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>();
	if (!IsValid(Hiring)) return false;
	TGuardValue<bool> Guard(bPayrollInProgress, true);
	const int32 Wage = Hiring->GetWageForBusinessDay(CurrentBusinessDay);
	const int32 PreviousWage = DailyEmployeeWageGold;
	DailyEmployeeWageGold = Wage;
	if (!ApplyForcedExpense(Wage))
	{
		DailyEmployeeWageGold = PreviousWage;
		return false;
	}
	LastPayrollProcessedBusinessDay = CurrentBusinessDay;
	OnDailySalesChanged.Broadcast();
	return true;
}

void ACSStoreGameState::BeginPlay()
{
	Super::BeginPlay();
}

void ACSStoreGameState::SetStorePhase(const FCSStorePhaseData& PhaseData)
{
	CurrentPhase = PhaseData.Phase;
	CurrentPhaseDisplayName = PhaseData.DisplayName;
	bCanRestock = PhaseData.bCanRestock;
	bCanCheckout = PhaseData.bCanCheckout;
	bCanSpawnCustomers = PhaseData.bCanSpawnCustomers;
	if (CurrentPhase != ECStorePhase::Open && bIsStoreOpen)
	{
		// 정산 등 영업 시간 밖의 단계로 바뀌면 영업 상태도 함께 종료합니다.
		bIsStoreOpen = false;
		OnStoreOpenStateChanged.Broadcast(false);
	}

	SetRemainingSeconds(0.0f);
	OnStorePhaseChanged.Broadcast(CurrentPhase);
}

void ACSStoreGameState::SetGameClock(
	float NewGameMinutes,
	float NewGameMinutesPerRealSecond,
	bool bNewClockRunning)
{
	GameMinutesPerRealSecond = FMath::Max(0.0f, NewGameMinutesPerRealSecond);
	SetGameClockRunning(bNewClockRunning);
	SetCurrentGameMinutes(NewGameMinutes);
}

void ACSStoreGameState::SetClosingTimeSettings(
	float NewClosingTimeMinutes,
	float NewCountdownMinutes)
{
	AutomaticClosingTimeMinutes = FMath::Max(0.0f, NewClosingTimeMinutes);
	ClosingClockCountdownMinutes = FMath::Max(1.0f, NewCountdownMinutes);
}

void ACSStoreGameState::SetGameClockRunning(bool bNewClockRunning)
{
	if (bIsGameClockRunning == bNewClockRunning)
	{
		return;
	}

	bIsGameClockRunning = bNewClockRunning;
	OnGameClockRunningChanged.Broadcast(bIsGameClockRunning);
}

void ACSStoreGameState::SetCurrentGameMinutes(float NewGameMinutes)
{
	// 다음 날 새벽 02:00까지 영업 시간을 유지할 수 있도록 26:00까지 허용합니다.
	CurrentGameMinutes = FMath::Clamp(NewGameMinutes, 0.0f, 1560.0f);
	OnGameTimeChanged.Broadcast(CurrentGameMinutes);
}

void ACSStoreGameState::SetEndOfDayTask(ECEndOfDayTask NewTask)
{
	CurrentEndOfDayTask = NewTask;
	OnEndOfDayTaskChanged.Broadcast(CurrentEndOfDayTask);
}

void ACSStoreGameState::RequestDailySummary()
{
	bIsDailySummaryPending = true;
	OnDailySummaryRequested.Broadcast();
}

void ACSStoreGameState::CompleteDailySummary()
{
	if (bIsDailySummaryPending)
	{
		PreviousDailyNetProfitGold = GetDailyNetProfitGold();
	}
	bIsDailySummaryPending = false;
}

bool ACSStoreGameState::CanChangeStoreOpenState() const
{
	return (CurrentPhase == ECStorePhase::Preparation ||
		CurrentPhase == ECStorePhase::Open) && !bIsDailySummaryPending;
}

bool ACSStoreGameState::SetStoreOpen(bool bShouldOpen)
{
	if (!CanChangeStoreOpenState())
	{
		return false;
	}

	if (bIsStoreOpen == bShouldOpen)
	{
		return true;
	}

	bIsStoreOpen = bShouldOpen;
	if (bIsStoreOpen)
	{
		// 당일에 영업을 시작한 이력을 남겨, 이후 중지 상태를 영업 종료로 표시합니다.
		bHasStoreOpenedToday = true;
	}
	OnStoreOpenStateChanged.Broadcast(bIsStoreOpen);
	return true;
}

void ACSStoreGameState::SetRemainingSeconds(float NewRemainingSeconds)
{
	RemainingSeconds = FMath::Max(0.0f, NewRemainingSeconds);
	OnRemainingTimeChanged.Broadcast(RemainingSeconds);
}

int32 ACSStoreGameState::GetCurrentFunds() const
{
	return CurrentFunds;
}

bool ACSStoreGameState::CanAfford(int32 Amount) const
{
	return Amount >= 0 && CurrentFunds >= Amount;
}

bool ACSStoreGameState::TrySpendFunds(int32 Amount)
{
	if (Amount < 0 || !CanAfford(Amount))
	{
		return false;
	}

	if (Amount == 0)
	{
		return true;
	}

	const int64 NewDailyExpense =
		static_cast<int64>(DailyExpenseGold) + Amount;
	if (NewDailyExpense > MAX_int32)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameState] 일일 실제 지출 합계가 int32 범위를 벗어나 지출을 처리하지 않습니다. 현재 지출=%dG, 요청=%dG"),
			DailyExpenseGold,
			Amount);
		return false;
	}

	CurrentFunds -= Amount;
	DailyExpenseGold = static_cast<int32>(NewDailyExpense);
	OnFundsChanged.Broadcast(CurrentFunds);
	OnDailySalesChanged.Broadcast();
	return true;
}

bool ACSStoreGameState::ApplyForcedExpense(int32 Amount)
{
	if (Amount < 0)
	{
		return false;
	}

	if (Amount == 0)
	{
		return true;
	}

	const int64 NewFunds = static_cast<int64>(CurrentFunds) - Amount;
	const int64 NewDailyExpense =
		static_cast<int64>(DailyExpenseGold) + Amount;
	if (NewFunds < MIN_int32 || NewFunds > MAX_int32 ||
		NewDailyExpense > MAX_int32)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameState] 강제 지출 %dG 적용 시 자금 또는 일일 지출이 int32 범위를 벗어나 처리하지 않습니다. 현재 자금=%dG, 현재 지출=%dG"),
			Amount,
			CurrentFunds,
			DailyExpenseGold);
		return false;
	}

	CurrentFunds = static_cast<int32>(NewFunds);
	DailyExpenseGold = static_cast<int32>(NewDailyExpense);
	OnFundsChanged.Broadcast(CurrentFunds);
	OnDailySalesChanged.Broadcast();
	return true;
}

bool ACSStoreGameState::AddFunds(int32 Amount)
{
	if (Amount < 0)
	{
		return false;
	}

	if (Amount == 0)
	{
		return true;
	}

	const int64 NewFunds = static_cast<int64>(CurrentFunds) + Amount;
	if (NewFunds > MAX_int32)
	{
		return false;
	}

	CurrentFunds = static_cast<int32>(NewFunds);
	OnFundsChanged.Broadcast(CurrentFunds);
	return true;
}

bool ACSStoreGameState::InitializeStoreProgression(
	UCSStoreProgressionDataAsset* InProgressionDataAsset)
{
	if (!IsValid(InProgressionDataAsset))
	{
		return false;
	}

	StoreProgressionDataAsset = InProgressionDataAsset;
	const int32 PreviousExperience = CurrentStoreExperience;
	const int32 PreviousLevel = CurrentStoreLevel;
	const int32 PreviousReputationScore = CurrentReputationScore;
	const ECSReputationGrade PreviousReputationGrade = CurrentReputationGrade;

	const int32 MaximumExperience =
		StoreProgressionDataAsset->GetMaximumStoreExperience();
	CurrentStoreExperience = FMath::Clamp(
		StoreProgressionDataAsset->InitialStoreExperience,
		0,
		MaximumExperience);
	CurrentStoreLevel = StoreProgressionDataAsset->GetStoreLevelForExperience(
		CurrentStoreExperience);

	const int32 MinimumReputationScore = FMath::Max(
		0,
		StoreProgressionDataAsset->MinimumReputationScore);
	const int32 MaximumReputationScore = FMath::Max(
		MinimumReputationScore,
		StoreProgressionDataAsset->MaximumReputationScore);
	CurrentReputationScore = FMath::Clamp(
		StoreProgressionDataAsset->InitialReputationScore,
		MinimumReputationScore,
		MaximumReputationScore);
	CurrentReputationGrade = StoreProgressionDataAsset->GetReputationGradeForScore(
		CurrentReputationScore);
	DailyStartingReputationScore = CurrentReputationScore;

	if (CurrentStoreExperience != PreviousExperience)
	{
		OnStoreExperienceChanged.Broadcast(CurrentStoreExperience);
	}
	if (CurrentStoreLevel != PreviousLevel)
	{
		OnStoreLevelChanged.Broadcast(CurrentStoreLevel);
	}
	if (CurrentReputationScore != PreviousReputationScore)
	{
		OnReputationChanged.Broadcast(CurrentReputationScore);
	}
	if (CurrentReputationGrade != PreviousReputationGrade)
	{
		OnReputationGradeChanged.Broadcast(CurrentReputationGrade);
	}

	return true;
}

bool ACSStoreGameState::InitializeFreshnessSettings(
	UCSFreshnessSettingsDataAsset* InFreshnessSettingsDataAsset)
{
	if (!IsValid(InFreshnessSettingsDataAsset))
	{
		return false;
	}

	FreshnessSettingsDataAsset = InFreshnessSettingsDataAsset;
	return true;
}

UCSFreshnessSettingsDataAsset* ACSStoreGameState::GetFreshnessSettings() const
{
	return FreshnessSettingsDataAsset;
}

bool ACSStoreGameState::InitializeStoreEconomy(
	UCSStoreEconomyDataAsset* InStoreEconomyDataAsset)
{
	if (!IsValid(InStoreEconomyDataAsset) ||
		!InStoreEconomyDataAsset->IsConfigurationValid())
	{
		return false;
	}

	StoreEconomyDataAsset = InStoreEconomyDataAsset;
	CurrentFunds = StoreEconomyDataAsset->InitialFunds;
	OnFundsChanged.Broadcast(CurrentFunds);
	return true;
}

bool ACSStoreGameState::ProcessDailyRent()
{
	if (LastRentProcessedBusinessDay == CurrentBusinessDay)
	{
		return true;
	}

	LastRentProcessedBusinessDay = CurrentBusinessDay;
	DailyRentGold = 0;
	if (!IsValid(StoreEconomyDataAsset))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameState] 상점 경제 설정이 없어 %d일차 임대료 처리를 생략합니다."),
			CurrentBusinessDay);
		return false;
	}

	if (!StoreEconomyDataAsset->IsRentPaymentDay(CurrentBusinessDay))
	{
		return true;
	}

	const int32 RentGold =
		StoreEconomyDataAsset->GetRentGoldForBusinessDay(CurrentBusinessDay);
	if (!ApplyForcedExpense(RentGold))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameState] %d일차 임대료 %dG 차감에 실패했습니다."),
			CurrentBusinessDay,
			RentGold);
		return false;
	}

	DailyRentGold = RentGold;
	OnDailySalesChanged.Broadcast();
	return true;
}

int32 ACSStoreGameState::GetDailyRentGold() const
{
	return DailyRentGold;
}

int32 ACSStoreGameState::GetCurrentRentGold() const
{
	return IsValid(StoreEconomyDataAsset)
		? StoreEconomyDataAsset->GetRentGoldForBusinessDay(CurrentBusinessDay)
		: 0;
}

int32 ACSStoreGameState::GetNextRentPaymentBusinessDay() const
{
	if (!IsValid(StoreEconomyDataAsset) ||
		!StoreEconomyDataAsset->IsConfigurationValid())
	{
		return 0;
	}

	const int64 CurrentDay = CurrentBusinessDay;
	const int64 FirstPaymentDay = StoreEconomyDataAsset->FirstRentBusinessDay;
	const int64 PaymentInterval = StoreEconomyDataAsset->RentPaymentIntervalDays;
	if (CurrentDay < FirstPaymentDay)
	{
		return static_cast<int32>(FirstPaymentDay);
	}

	const bool bIsTodayPaymentDay =
		StoreEconomyDataAsset->IsRentPaymentDay(CurrentBusinessDay);
	if (bIsTodayPaymentDay &&
		LastRentProcessedBusinessDay != CurrentBusinessDay)
	{
		return CurrentBusinessDay;
	}

	const int64 DaysSinceFirstPayment = CurrentDay - FirstPaymentDay;
	const int64 DaysIntoCurrentInterval = DaysSinceFirstPayment % PaymentInterval;
	const int64 DaysUntilNextPayment = PaymentInterval - DaysIntoCurrentInterval;
	const int64 NextPaymentDay = CurrentDay + DaysUntilNextPayment;
	return NextPaymentDay <= MAX_int32
		? static_cast<int32>(NextPaymentDay)
		: 0;
}

int32 ACSStoreGameState::GetDaysUntilNextRentPayment() const
{
	const int32 NextPaymentDay = GetNextRentPaymentBusinessDay();
	if (NextPaymentDay <= 0)
	{
		return 0;
	}

	const int64 DaysUntilPayment =
		static_cast<int64>(NextPaymentDay) - CurrentBusinessDay;
	return static_cast<int32>(FMath::Clamp<int64>(
		DaysUntilPayment,
		0,
		MAX_int32));
}

int32 ACSStoreGameState::GetNextRentGold() const
{
	if (!IsValid(StoreEconomyDataAsset) ||
		!StoreEconomyDataAsset->IsConfigurationValid())
	{
		return 0;
	}

	const int32 NextPaymentDay = GetNextRentPaymentBusinessDay();
	return NextPaymentDay > 0
		? StoreEconomyDataAsset->GetRentGoldForBusinessDay(NextPaymentDay)
		: 0;
}

bool ACSStoreGameState::IsRentPaymentDay() const
{
	return IsValid(StoreEconomyDataAsset) &&
		StoreEconomyDataAsset->IsRentPaymentDay(CurrentBusinessDay);
}

int32 ACSStoreGameState::GetStoreExperience() const
{
	return CurrentStoreExperience;
}

int32 ACSStoreGameState::GetStoreLevel() const
{
	return CurrentStoreLevel;
}

FText ACSStoreGameState::GetStoreLevelDisplayName() const
{
	return GetStoreLevelDisplayNameForLevel(CurrentStoreLevel);
}

FText ACSStoreGameState::GetStoreLevelDisplayNameForLevel(const int32 StoreLevel) const
{
	const int32 SafeLevel = FMath::Clamp(StoreLevel, 1, 10);
	return IsValid(StoreProgressionDataAsset)
		? StoreProgressionDataAsset->GetStoreLevelDisplayName(SafeLevel)
		: FText::Format(
			NSLOCTEXT("StoreLevel", "FallbackLevelName", "상점 레벨 {0}"),
			FText::AsNumber(SafeLevel));
}

int32 ACSStoreGameState::GetStoreExperienceInCurrentLevel() const
{
	if (!IsValid(StoreProgressionDataAsset))
	{
		return 0;
	}

	// 누적 경험치에서 현재 레벨 시작 누적값을 빼서 레벨 내부 진행량으로 바꿉니다.
	const int32 CurrentLevelStart =
		StoreProgressionDataAsset->GetRequiredTotalExperienceForLevel(CurrentStoreLevel);
	return FMath::Max(0, CurrentStoreExperience - CurrentLevelStart);
}

int32 ACSStoreGameState::GetStoreExperienceRequiredForNextLevel() const
{
	if (!IsValid(StoreProgressionDataAsset) || IsMaximumStoreLevel())
	{
		return 0;
	}

	const int32 CurrentLevelStart =
		StoreProgressionDataAsset->GetRequiredTotalExperienceForLevel(CurrentStoreLevel);
	const int32 NextLevelStart =
		StoreProgressionDataAsset->GetRequiredTotalExperienceForLevel(CurrentStoreLevel + 1);
	return FMath::Max(0, NextLevelStart - CurrentLevelStart);
}

float ACSStoreGameState::GetStoreLevelProgress() const
{
	if (IsMaximumStoreLevel())
	{
		return 1.0f;
	}

	const int32 RequiredExperience = GetStoreExperienceRequiredForNextLevel();
	return RequiredExperience > 0
		? FMath::Clamp(
			static_cast<float>(GetStoreExperienceInCurrentLevel()) /
			static_cast<float>(RequiredExperience),
			0.0f,
			1.0f)
		: 0.0f;
}

bool ACSStoreGameState::IsMaximumStoreLevel() const
{
	if (!IsValid(StoreProgressionDataAsset))
	{
		return false;
	}

	const int32 MaximumLevel = FMath::Min(
		StoreProgressionDataAsset->RequiredTotalExperience.Num(),
		10);
	return MaximumLevel > 0 && CurrentStoreLevel >= MaximumLevel;
}

int32 ACSStoreGameState::GetReputationScore() const
{
	return CurrentReputationScore;
}

ECSReputationGrade ACSStoreGameState::GetReputationGrade() const
{
	return CurrentReputationGrade;
}

bool ACSStoreGameState::AddStoreExperience(int32 Amount)
{
	if (!IsValid(StoreProgressionDataAsset) || Amount < 0)
	{
		return false;
	}

	const int32 MaximumExperience =
		StoreProgressionDataAsset->GetMaximumStoreExperience();
	const int64 AddedExperience =
		static_cast<int64>(CurrentStoreExperience) + Amount;
	const int32 NewExperience = static_cast<int32>(FMath::Clamp<int64>(
		AddedExperience,
		static_cast<int64>(0),
		static_cast<int64>(MaximumExperience)));
	if (NewExperience == CurrentStoreExperience)
	{
		return true;
	}

	CurrentStoreExperience = NewExperience;
	OnStoreExperienceChanged.Broadcast(CurrentStoreExperience);

	const int32 CalculatedLevel =
		StoreProgressionDataAsset->GetStoreLevelForExperience(CurrentStoreExperience);
	const int32 NewLevel = FMath::Max(CurrentStoreLevel, CalculatedLevel);
	if (NewLevel != CurrentStoreLevel)
	{
		CurrentStoreLevel = NewLevel;
		OnStoreLevelChanged.Broadcast(CurrentStoreLevel);
	}

	return true;
}

int32 ACSStoreGameState::GetRemainingStoreExperience() const
{
	return IsValid(StoreProgressionDataAsset) && !IsMaximumStoreLevel()
		? FMath::Max(0, StoreProgressionDataAsset->GetMaximumStoreExperience() - CurrentStoreExperience) : 0;
}

int32 ACSStoreGameState::GetRemainingReputation() const
{
	if (!IsValid(StoreProgressionDataAsset)) return 0;
	const int32 Maximum = FMath::Max(FMath::Max(0, StoreProgressionDataAsset->MinimumReputationScore), StoreProgressionDataAsset->MaximumReputationScore);
	return FMath::Max(0, Maximum - CurrentReputationScore);
}

bool ACSStoreGameState::AddReputation(int32 Delta, const FString& Reason)
{
	if (!IsValid(StoreProgressionDataAsset))
	{
		return false;
	}

	const int32 MinimumReputationScore = FMath::Max(
		0,
		StoreProgressionDataAsset->MinimumReputationScore);
	const int32 MaximumReputationScore = FMath::Max(
		MinimumReputationScore,
		StoreProgressionDataAsset->MaximumReputationScore);
	const int32 PreviousReputationScore = CurrentReputationScore;
	const int64 AddedReputation =
		static_cast<int64>(CurrentReputationScore) + Delta;
	const int32 NewReputationScore = static_cast<int32>(FMath::Clamp<int64>(
		AddedReputation,
		static_cast<int64>(MinimumReputationScore),
		static_cast<int64>(MaximumReputationScore)));
	if (NewReputationScore == CurrentReputationScore)
	{
		return true;
	}

	CurrentReputationScore = NewReputationScore;
	OnReputationChanged.Broadcast(CurrentReputationScore);

	// 어디서/왜 평판이 바뀌었는지 한곳에서 남김 - 호출부마다 따로 로그를 안 찍어도 되게
	UE_LOG(LogTemp, Warning, TEXT("[평판 변동] %s%s%d → %d (%+d)"),
		Reason.IsEmpty() ? TEXT("") : *Reason,
		Reason.IsEmpty() ? TEXT("") : TEXT(" - "),
		PreviousReputationScore, CurrentReputationScore, Delta);

	const ECSReputationGrade NewReputationGrade =
		StoreProgressionDataAsset->GetReputationGradeForScore(CurrentReputationScore);
	if (NewReputationGrade != CurrentReputationGrade)
	{
		CurrentReputationGrade = NewReputationGrade;
		OnReputationGradeChanged.Broadcast(CurrentReputationGrade);
	}

	return true;
}

bool ACSStoreGameState::IsStoreLevelRequirementMet(int32 RequiredLevel) const
{
	return CurrentStoreLevel >= FMath::Max(1, RequiredLevel);
}

bool ACSStoreGameState::IsReputationRequirementMet(
	ECSReputationGrade RequiredGrade) const
{
	return static_cast<uint8>(CurrentReputationGrade) >=
		static_cast<uint8>(RequiredGrade);
}

bool ACSStoreGameState::RecordCheckoutTransaction(
	const TArray<FCSCheckoutLineItem>& LineItems)
{
	if (LineItems.IsEmpty())
	{
		return false;
	}

	FCSCheckoutTransaction Transaction;
	Transaction.TransactionId = NextTransactionId;
	Transaction.BusinessDay = CurrentBusinessDay;
	Transaction.GameMinutes = CurrentGameMinutes;
	Transaction.LineItems = LineItems;

	for (int32 LineIndex = 0; LineIndex < LineItems.Num(); ++LineIndex)
	{
		const FCSCheckoutLineItem& LineItem = LineItems[LineIndex];
		// 수량 또는 가격이 잘못된 주문은 매출 데이터에 기록하지 않습니다.
		if (LineItem.ProductId.IsNone() || LineItem.Quantity <= 0 ||
			LineItem.UnitSalePrice < 0 || LineItem.UnitPurchasePrice < 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameState] 유효하지 않은 계산 주문이 있어 거래를 기록하지 않습니다."));
			return false;
		}

		// UI에서 전달한 합계 대신 단가와 수량으로 다시 계산하여 정산값을 신뢰할 수 있게 합니다.
		const int64 LineSalesGold =
			static_cast<int64>(LineItem.UnitSalePrice) * LineItem.Quantity;
		const int64 LineCostGold =
			static_cast<int64>(LineItem.UnitPurchasePrice) * LineItem.Quantity;
		const int64 NewTotalSalesGold =
			static_cast<int64>(Transaction.TotalSalesGold) + LineSalesGold;
		const int64 NewTotalCostGold =
			static_cast<int64>(Transaction.TotalCostGold) + LineCostGold;
		if (NewTotalSalesGold > MAX_int32 || NewTotalCostGold > MAX_int32)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[StoreGameState] 계산 금액이 int32 범위를 초과해 거래를 기록하지 않습니다."));
			return false;
		}

		Transaction.LineItems[LineIndex].LineTotalSalePrice =
			static_cast<int32>(LineSalesGold);
		Transaction.TotalSalesGold = static_cast<int32>(NewTotalSalesGold);
		Transaction.TotalCostGold = static_cast<int32>(NewTotalCostGold);
	}

	Transaction.TotalProfitGold = Transaction.TotalSalesGold - Transaction.TotalCostGold;
	if (!AddFunds(Transaction.TotalSalesGold))
	{
		return false;
	}

	++NextTransactionId;
	DailyCheckoutTransactions.Add(Transaction);
	DailySalesGold += Transaction.TotalSalesGold;
	DailyCostGold += Transaction.TotalCostGold;
	DailyProfitGold += Transaction.TotalProfitGold;
	++CompletedCheckoutCount;

	for (const FCSCheckoutLineItem& LineItem : LineItems)
	{
		DailySoldQuantity += LineItem.Quantity;
	}

	if (IsValid(StoreProgressionDataAsset) &&
		StoreProgressionDataAsset->ExperiencePerSoldItem > 0)
	{
		int64 TransactionSoldQuantity = 0;
		for (const FCSCheckoutLineItem& LineItem : LineItems)
		{
			TransactionSoldQuantity = FMath::Min<int64>(
				static_cast<int64>(MAX_int32),
				TransactionSoldQuantity + LineItem.Quantity);
		}

		const int64 EarnedExperience = TransactionSoldQuantity *
			StoreProgressionDataAsset->ExperiencePerSoldItem;
		AddStoreExperience(static_cast<int32>(FMath::Min<int64>(
			EarnedExperience,
			MAX_int32)));
	}

	OnCheckoutRecorded.Broadcast(Transaction);
	OnDailySalesChanged.Broadcast();
	return true;
}

bool ACSStoreGameState::RecordDisposedProduct(const ACSProductPickupActor* Product)
{
	if (!IsValid(Product))
	{
		return false;
	}

	const FDataTableRowHandle ProductRow = Product->GetProductRow();
	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("ACSStoreGameState::RecordDisposedProduct"));
	if (ProductData == nullptr || ProductData->PurchasePrice < 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameState] 폐기 상품의 매입가를 확인할 수 없어 손실을 기록하지 않습니다."));
		return false;
	}

	const int64 NewWasteLoss =
		static_cast<int64>(DailyWasteLossGold) + ProductData->PurchasePrice;
	if (NewWasteLoss > MAX_int32)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameState] 당일 폐기 손실이 int32 범위를 초과해 기록하지 않습니다."));
		return false;
	}

	DailyWasteLossGold = static_cast<int32>(NewWasteLoss);
	const FName ProductId = Product->GetProductId();
	if (!ProductId.IsNone())
	{
		int32& DisposedQuantity = DailyDisposedProductQuantities.FindOrAdd(ProductId);
		if (DisposedQuantity < MAX_int32)
		{
			++DisposedQuantity;
		}
		DailyDisposedProductNames.FindOrAdd(ProductId) = ProductData->DisplayName;
	}
	OnDailySalesChanged.Broadcast();
	return true;
}

bool ACSStoreGameState::RecordOrderExpense(
	int32 Amount,
	bool bFacilityOrder,
	bool bImmediateDelivery)
{
	if (Amount < 0)
	{
		return false;
	}

	int32* CategoryTotal = nullptr;
	if (bFacilityOrder)
	{
		CategoryTotal = bImmediateDelivery
			? &DailyQuickFacilityOrderExpenseGold
			: &DailyFacilityOrderExpenseGold;
	}
	else
	{
		CategoryTotal = bImmediateDelivery
			? &DailyQuickProductOrderExpenseGold
			: &DailyProductOrderExpenseGold;
	}

	if (Amount > MAX_int32 - *CategoryTotal)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[StoreGameState] 발주 종류별 일일 지출이 int32 범위를 초과해 기록하지 않습니다."));
		return false;
	}

	*CategoryTotal += Amount;
	OnDailySalesChanged.Broadcast();
	return true;
}

bool ACSStoreGameState::RecordShopliftLoss(int32 LostGold,const TArray<FCSStolenProductRecord>& StolenProducts)
{
	if (LostGold < 0 || LostGold > MAX_int32 - DailyShopliftLossGold)
	{
		return false;
	}

	DailyShopliftLossGold += LostGold;

	// 폐기와 같은 형태로 쌓아 두면 상품 분석 화면이 같은 코드로 읽어갈 수 있습니다.
	for (const FCSStolenProductRecord& StolenProduct : StolenProducts)
	{
		if (StolenProduct.ProductId.IsNone() || StolenProduct.Quantity <= 0)
		{
			continue;
		}

		int32& StolenQuantity = DailyStolenProductQuantities.FindOrAdd(StolenProduct.ProductId);
		StolenQuantity = static_cast<int32>(FMath::Min<int64>(
			static_cast<int64>(StolenQuantity) + StolenProduct.Quantity,
			MAX_int32));
		DailyStolenProductNames.FindOrAdd(StolenProduct.ProductId) = StolenProduct.DisplayName;
	}

	OnDailySalesChanged.Broadcast();
	return true;
}

int32 ACSStoreGameState::GetDailyTotalLossGold() const
{
	// 기존 Blueprint 호환 이름은 유지하되 현재 기획의 실제 총지출을 반환합니다.
	return GetDailyTotalExpenseGold();
}

int32 ACSStoreGameState::GetDailyTotalExpenseGold() const
{
	return DailyExpenseGold;
}

int32 ACSStoreGameState::GetDailySeparateLossGold() const
{
	return static_cast<int32>(FMath::Min<int64>(
		static_cast<int64>(DailyWasteLossGold) + DailyShopliftLossGold,
		MAX_int32));
}

int32 ACSStoreGameState::GetDailyNetProfitGold() const
{
	const int64 NetProfit =
		static_cast<int64>(DailySalesGold) - DailyExpenseGold;
	return static_cast<int32>(FMath::Clamp<int64>(
		NetProfit,
		static_cast<int64>(MIN_int32),
		static_cast<int64>(MAX_int32)));
}

int32 ACSStoreGameState::GetDailyNetProfitDeltaGold() const
{
	const int64 Delta =
		static_cast<int64>(GetDailyNetProfitGold()) - PreviousDailyNetProfitGold;
	return static_cast<int32>(FMath::Clamp<int64>(
		Delta,
		static_cast<int64>(MIN_int32),
		static_cast<int64>(MAX_int32)));
}

int32 ACSStoreGameState::GetDailyReputationDelta() const
{
	const int64 Delta =
		static_cast<int64>(CurrentReputationScore) - DailyStartingReputationScore;
	return static_cast<int32>(FMath::Clamp<int64>(
		Delta,
		static_cast<int64>(MIN_int32),
		static_cast<int64>(MAX_int32)));
}

void ACSStoreGameState::BeginNextBusinessDay()
{
	++CurrentBusinessDay;
	// 새 영업일에는 아직 개점하지 않았으므로 영업 시작 이력을 초기화합니다.
	bHasStoreOpenedToday = false;
	DailySalesGold = 0;
	DailyCostGold = 0;
	DailyProfitGold = 0;
	DailySoldQuantity = 0;
	CompletedCheckoutCount = 0;
	DailyWasteLossGold = 0;
	DailyRentGold = 0;
	DailyExpenseGold = 0;
	DailyMagicCircleUpgradeExpenseGold = 0;
	DailyEmployeeHireExpenseGold = 0;
	DailyEmployeeWageGold = 0;
	DailyProductOrderExpenseGold = 0;
	DailyFacilityOrderExpenseGold = 0;
	DailyQuickProductOrderExpenseGold = 0;
	DailyQuickFacilityOrderExpenseGold = 0;
	DailyShopliftLossGold = 0;
	DailyDisposedProductQuantities.Reset();
	DailyDisposedProductNames.Reset();
	DailyStolenProductQuantities.Reset();
	DailyStolenProductNames.Reset();
	DailyCheckoutTransactions.Reset();
	NextTransactionId = 1;
	DailyStartingReputationScore = CurrentReputationScore;

	// 다음 날 HUD와 정산 화면이 이전 날의 값을 유지하지 않도록 갱신을 알립니다.
	OnDailySalesChanged.Broadcast();
	// 월드에 남아 있는 상품이 하루 경과에 따른 변질 상태를 갱신할 수 있도록 알립니다.
	OnBusinessDayStarted.Broadcast(CurrentBusinessDay);
}

// -----------------------------------------------------------------------------
// 세이브 / 로드
// -----------------------------------------------------------------------------

void ACSStoreGameState::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSStoreStateRecord& Record = OutSave.StoreState;
	Record.CurrentBusinessDay = CurrentBusinessDay;
	Record.CurrentFunds = CurrentFunds;
	Record.CurrentStoreExperience = CurrentStoreExperience;
	Record.CurrentStoreLevel = CurrentStoreLevel;
	Record.CurrentReputationScore = CurrentReputationScore;
	Record.CurrentReputationGrade = CurrentReputationGrade;
	Record.MagicCircleLevel = MagicCircleLevel;
	Record.PreviousDailyNetProfitGold = PreviousDailyNetProfitGold;
	Record.DailyStartingReputationScore = DailyStartingReputationScore;

	// 로드 직후 임대료·인건비 이중 청구를 방지
	Record.LastRentProcessedBusinessDay = LastRentProcessedBusinessDay;
	Record.LastPayrollProcessedBusinessDay = LastPayrollProcessedBusinessDay;

	// 불러온 뒤 자유 시간 복귀 여부를 가르는 기준
	Record.SavedPhase = CurrentPhase;
	Record.bHasStoreOpenedToday = bHasStoreOpenedToday;

	// 영업 종료 직후 저장이므로 그날 집계를 기록해 불러온 뒤 정산 결과를 유지
	Record.DailySalesGold = DailySalesGold;
	Record.DailyCostGold = DailyCostGold;
	Record.DailyProfitGold = DailyProfitGold;
	Record.DailySoldQuantity = DailySoldQuantity;
	Record.CompletedCheckoutCount = CompletedCheckoutCount;
	Record.DailyWasteLossGold = DailyWasteLossGold;
	Record.DailyRentGold = DailyRentGold;
	Record.DailyExpenseGold = DailyExpenseGold;
	Record.DailyMagicCircleUpgradeExpenseGold = DailyMagicCircleUpgradeExpenseGold;
	Record.DailyEmployeeHireExpenseGold = DailyEmployeeHireExpenseGold;
	Record.DailyEmployeeWageGold = DailyEmployeeWageGold;
	Record.DailyProductOrderExpenseGold = DailyProductOrderExpenseGold;
	Record.DailyFacilityOrderExpenseGold = DailyFacilityOrderExpenseGold;
	Record.DailyQuickProductOrderExpenseGold = DailyQuickProductOrderExpenseGold;
	Record.DailyQuickFacilityOrderExpenseGold = DailyQuickFacilityOrderExpenseGold;
	Record.DailyShopliftLossGold = DailyShopliftLossGold;
	Record.DailyDisposedProductQuantities = DailyDisposedProductQuantities;
	Record.DailyDisposedProductNames = DailyDisposedProductNames;
	Record.DailyStolenProductQuantities = DailyStolenProductQuantities;
	Record.DailyStolenProductNames = DailyStolenProductNames;
	Record.DailyCheckoutTransactions = DailyCheckoutTransactions;
	Record.NextTransactionId = NextTransactionId;

	// 시계·영업 여부·정산 대기(bIsDailySummaryPending) 등은 기록하지 않음
	// 불러온 뒤 GameMode가 SavedPhase 기준으로 다시 확정
}

void ACSStoreGameState::RestoreSaveData(const UCSSaveGame& InSave)
{
	const FCSStoreStateRecord& Record = InSave.StoreState;
	CurrentBusinessDay = Record.CurrentBusinessDay;
	CurrentFunds = Record.CurrentFunds;
	CurrentStoreExperience = Record.CurrentStoreExperience;
	CurrentStoreLevel = Record.CurrentStoreLevel;
	CurrentReputationScore = Record.CurrentReputationScore;
	CurrentReputationGrade = Record.CurrentReputationGrade;
	MagicCircleLevel = Record.MagicCircleLevel;
	PreviousDailyNetProfitGold = Record.PreviousDailyNetProfitGold;
	DailyStartingReputationScore = Record.DailyStartingReputationScore;
	LastRentProcessedBusinessDay = Record.LastRentProcessedBusinessDay;
	LastPayrollProcessedBusinessDay = Record.LastPayrollProcessedBusinessDay;

	// 단계는 여기서 적용하지 않고 GameMode가 모든 참가자 복원 후 이어서 적용
	RestoredSavePhase = Record.SavedPhase;
	bHasStoreOpenedToday = Record.bHasStoreOpenedToday;

	DailySalesGold = Record.DailySalesGold;
	DailyCostGold = Record.DailyCostGold;
	DailyProfitGold = Record.DailyProfitGold;
	DailySoldQuantity = Record.DailySoldQuantity;
	CompletedCheckoutCount = Record.CompletedCheckoutCount;
	DailyWasteLossGold = Record.DailyWasteLossGold;
	DailyRentGold = Record.DailyRentGold;
	DailyExpenseGold = Record.DailyExpenseGold;
	DailyMagicCircleUpgradeExpenseGold = Record.DailyMagicCircleUpgradeExpenseGold;
	DailyEmployeeHireExpenseGold = Record.DailyEmployeeHireExpenseGold;
	DailyEmployeeWageGold = Record.DailyEmployeeWageGold;
	DailyProductOrderExpenseGold = Record.DailyProductOrderExpenseGold;
	DailyFacilityOrderExpenseGold = Record.DailyFacilityOrderExpenseGold;
	DailyQuickProductOrderExpenseGold = Record.DailyQuickProductOrderExpenseGold;
	DailyQuickFacilityOrderExpenseGold = Record.DailyQuickFacilityOrderExpenseGold;
	DailyShopliftLossGold = Record.DailyShopliftLossGold;
	DailyDisposedProductQuantities = Record.DailyDisposedProductQuantities;
	DailyDisposedProductNames = Record.DailyDisposedProductNames;
	DailyStolenProductQuantities = Record.DailyStolenProductQuantities;
	DailyStolenProductNames = Record.DailyStolenProductNames;
	DailyCheckoutTransactions = Record.DailyCheckoutTransactions;
	NextTransactionId = FMath::Max(1, Record.NextTransactionId);

	// UI가 이전 값을 유지하지 않도록 변경을 알림
	OnFundsChanged.Broadcast(CurrentFunds);
	OnDailySalesChanged.Broadcast();
	OnStoreExperienceChanged.Broadcast(CurrentStoreExperience);
	OnStoreLevelChanged.Broadcast(CurrentStoreLevel);
	OnReputationChanged.Broadcast(CurrentReputationScore);
	OnReputationGradeChanged.Broadcast(CurrentReputationGrade);
	OnMagicCircleUpgraded.Broadcast(MagicCircleLevel);

	// 일차 표시는 재추첨을 일으키지 않는 표시 전용 이벤트로 갱신
	OnBusinessDayRestored.Broadcast(CurrentBusinessDay);
}
