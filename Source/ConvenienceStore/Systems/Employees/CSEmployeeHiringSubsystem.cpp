#include "Systems/Employees/CSEmployeeHiringSubsystem.h"
#include "Systems/Audio/CSAudioSubsystem.h"

#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UCSEmployeeHiringSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSEmployeeHiringSaveRecord& Record = OutSave.EmployeeHiring;
	Record.Contracts.Reset(Contracts.Num());
	for (const FCSEmployeeContract& Contract : Contracts)
	{
		// 직원 액터 참조는 Transient이며 복원 시 다시 연결하므로 제외
		FCSEmployeeContractRecord ContractRecord;
		ContractRecord.Slot = Contract.Slot;
		ContractRecord.HireCost = Contract.HireCost;
		ContractRecord.DailyWage = Contract.DailyWage;
		ContractRecord.StartBusinessDay = Contract.StartBusinessDay;
		Record.Contracts.Add(ContractRecord);
	}
}

void UCSEmployeeHiringSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	TArray<FCSEmployeeContract> RestoredContracts;
	RestoredContracts.Reserve(InSave.EmployeeHiring.Contracts.Num());
	for (const FCSEmployeeContractRecord& ContractRecord : InSave.EmployeeHiring.Contracts)
	{
		FCSEmployeeContract Contract;
		Contract.Slot = ContractRecord.Slot;
		Contract.HireCost = ContractRecord.HireCost;
		Contract.DailyWage = ContractRecord.DailyWage;
		Contract.StartBusinessDay = ContractRecord.StartBusinessDay;
		Contract.Employee = nullptr;
		RestoredContracts.Add(Contract);
	}

	RestoreContracts(RestoredContracts);
}

void UCSEmployeeHiringSubsystem::RestoreContracts(const TArray<FCSEmployeeContract>& InContracts)
{
	UWorld* World = GetWorld();
	const ACSStoreGameState* State = nullptr;
	if (World != nullptr)
	{
		State = World->GetGameState<ACSStoreGameState>();
	}

	int32 CurrentBusinessDay = 1;
	if (IsValid(State))
	{
		CurrentBusinessDay = State->CurrentBusinessDay;
	}

	// 복원 시점에는 고용소가 BeginPlay에서 생성한 지원자가 이미 배치되어 있으므로
	// 새로 생성하지 않고 고용 자리가 같은 지원자를 수집
	TMap<ECSEmployeeHireSlot, ACSStoreEmployeeCharacter*> ApplicantsBySlot;
	if (World != nullptr)
	{
		for (TActorIterator<ACSStoreEmployeeCharacter> It(World); It; ++It)
		{
			ACSStoreEmployeeCharacter* Candidate = *It;
			if (IsValid(Candidate) && Candidate->RequiresHiring())
			{
				ApplicantsBySlot.Add(Candidate->GetHiringSlot(), Candidate);
			}
		}
	}

	Contracts.Reset(InContracts.Num());
	for (const FCSEmployeeContract& Source : InContracts)
	{
		FCSEmployeeContract Contract = Source;
		Contract.Employee = nullptr;
		if (ACSStoreEmployeeCharacter** Found = ApplicantsBySlot.Find(Contract.Slot))
		{
			Contract.Employee = *Found;
		}

		Contracts.Add(Contract);
	}

	// CanEmployeeWork()가 계약 목록을 조회하므로 계약 확정 후 직원 표시를 갱신
	for (const FCSEmployeeContract& Contract : Contracts)
	{
		if (ACSStoreEmployeeCharacter* Employee = Contract.Employee.Get())
		{
			Employee->RefreshEmploymentPresentation();
			continue;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[EmployeeHiring] 복원한 계약의 직원 액터를 찾지 못했습니다. 자리 %d, 시작 일차 %d (현재 %d일차). 맵에 고용소가 있는지 확인해 주세요."),
			static_cast<int32>(Contract.Slot),
			Contract.StartBusinessDay,
			CurrentBusinessDay);
	}

	OnEmployeeHired.Broadcast();
}

bool UCSEmployeeHiringSubsystem::IsSlotHired(ECSEmployeeHireSlot Slot) const
{
	return Contracts.ContainsByPredicate([Slot](const FCSEmployeeContract& Contract) { return Contract.Slot == Slot; });
}

int32 UCSEmployeeHiringSubsystem::GetTotalDailyWage() const
{
	return GetWageForBusinessDay(MAX_int32);
}

int32 UCSEmployeeHiringSubsystem::GetWageForBusinessDay(int32 BusinessDay) const
{
	int64 Total = 0;
	for (const FCSEmployeeContract& Contract : Contracts)
	{
		if (Contract.StartBusinessDay <= BusinessDay) Total += Contract.DailyWage;
	}
	return static_cast<int32>(FMath::Min<int64>(Total, MAX_int32));
}

int32 UCSEmployeeHiringSubsystem::GetNextStartBusinessDay() const
{
	const ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(State)) return 0;
	return State->CurrentPhase == ECStorePhase::Preparation
		? State->CurrentBusinessDay : State->CurrentBusinessDay + 1;
}

bool UCSEmployeeHiringSubsystem::CanEmployeeWork(const ACSStoreEmployeeCharacter* Employee) const
{
	const ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(Employee) || !IsValid(State)) return false;
	return Contracts.ContainsByPredicate([Employee, State](const FCSEmployeeContract& Contract)
	{
		return Contract.Employee.Get() == Employee && Contract.StartBusinessDay <= State->CurrentBusinessDay;
	});
}

bool UCSEmployeeHiringSubsystem::CanHire(const ACSStoreEmployeeCharacter* Applicant, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(State) || !IsValid(Applicant) || Applicant->GetWorld() != GetWorld() || !Applicant->RequiresHiring() ||
		!State->HasAuthority() || bHiringInProgress || State->bIsDailySummaryPending ||
		(State->CurrentPhase != ECStorePhase::Preparation && State->CurrentPhase != ECStorePhase::Open &&
		 State->CurrentPhase != ECStorePhase::ClosingFreeRoam) || State->CurrentBusinessDay >= MAX_int32)
	{
		OutReason = NSLOCTEXT("EmployeeHiring", "Unavailable", "지금은 고용할 수 없습니다.");
		return false;
	}
	const ECSEmployeeHireSlot Slot = Applicant->GetHiringSlot();
	if (Slot != ECSEmployeeHireSlot::FirstRestock && Slot != ECSEmployeeHireSlot::Checkout && Slot != ECSEmployeeHireSlot::SecondRestock)
	{
		OutReason = NSLOCTEXT("EmployeeHiring", "InvalidSlot", "고용 자리가 올바르게 설정되지 않았습니다.");
		return false;
	}
	if (IsSlotHired(Slot))
	{
		OutReason = NSLOCTEXT("EmployeeHiring", "Occupied", "이미 고용한 자리입니다.");
		return false;
	}
	const bool bCheckout = Applicant->CanHandleTask(ECSStoreEmployeeTaskType::CheckoutService);
	const bool bRestock = Applicant->CanHandleTask(ECSStoreEmployeeTaskType::RestockShelf);
	if ((Slot == ECSEmployeeHireSlot::Checkout && (!bCheckout || bRestock)) ||
		(Slot != ECSEmployeeHireSlot::Checkout && (!bRestock || bCheckout)))
	{
		OutReason = NSLOCTEXT("EmployeeHiring", "WrongRole", "고용 자리와 직원의 담당 업무가 일치하지 않습니다.");
		return false;
	}
	// 화면 외부에서 고용을 요청해도 동일한 레벨 조건을 검증합니다.
	if (!State->IsStoreLevelRequirementMet(State->GetEmployeeRequiredStoreLevel(Slot)))
	{
		OutReason = FText::Format(NSLOCTEXT("EmployeeHiring", "StoreLevelRequired", "가게 레벨 {0}이 필요합니다. (현재 {1})"),
			FText::AsNumber(State->GetEmployeeRequiredStoreLevel(Slot)), FText::AsNumber(State->GetStoreLevel()));
		return false;
	}
	if (!State->CanAfford(State->GetEmployeeHireCost(Slot)))
	{
		OutReason = NSLOCTEXT("EmployeeHiring", "InsufficientFunds", "고용비를 지불할 자금이 부족합니다.");
		return false;
	}
	return true;
}

bool UCSEmployeeHiringSubsystem::TryHire(ACSStoreEmployeeCharacter* Applicant, FText& OutReason)
{
	if (!CanHire(Applicant, OutReason)) return false;
	TGuardValue<bool> Guard(bHiringInProgress, true);
	ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	FCSEmployeeContract Contract;
	Contract.Slot = Applicant->GetHiringSlot();
	Contract.HireCost = State->GetEmployeeHireCost(Contract.Slot);
	Contract.DailyWage = State->GetEmployeeDailyWage(Contract.Slot);
	Contract.StartBusinessDay = GetNextStartBusinessDay();
	Contract.Employee = Applicant;
	// 잔액 변경 이벤트가 발행될 때도 계약과 화면의 인원 수가 일치하도록 먼저 예약합니다.
	Contracts.Add(Contract);
	if (!State->TryPayEmployeeHireCost(Contract.HireCost))
	{
		Contracts.Pop();
		OutReason = NSLOCTEXT("EmployeeHiring", "PaymentFailed", "고용비 결제에 실패했습니다.");
		return false;
	}
	Applicant->RefreshEmploymentPresentation();
	OnEmployeeHired.Broadcast();
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::EmployeeHireSuccess);
	return true;
}
