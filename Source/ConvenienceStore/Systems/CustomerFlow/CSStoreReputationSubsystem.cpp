#include "Systems/CustomerFlow/CSStoreReputationSubsystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Systems/SaveLoad/CSSaveGame.h"

void UCSStoreReputationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (UCSCustomerRuntimeSubsystem* RuntimeSubsystem = InWorld.GetSubsystem<UCSCustomerRuntimeSubsystem>())
	{
		RuntimeSubsystem->OnCheckoutCompleted.AddDynamic(this, &UCSStoreReputationSubsystem::HandleCheckoutCompleted);
		RuntimeSubsystem->OnCheckoutAbandoned.AddDynamic(this, &UCSStoreReputationSubsystem::HandleCheckoutAbandoned);
		RuntimeSubsystem->OnCustomerDespawned.AddDynamic(this, &UCSStoreReputationSubsystem::HandleCustomerDespawned);
		RuntimeSubsystem->OnPurchaseRejected.AddDynamic(this, &UCSStoreReputationSubsystem::HandlePurchaseRejected);
	}

	if (ACSStoreGameState* GameState = InWorld.GetGameState<ACSStoreGameState>())
	{
		GameState->OnDailySummaryRequested.AddDynamic(this, &UCSStoreReputationSubsystem::HandleDailySummaryRequested);
		GameState->OnBusinessDayStarted.AddDynamic(this, &UCSStoreReputationSubsystem::HandleBusinessDayStarted);
	}
}

void UCSStoreReputationSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UCSCustomerRuntimeSubsystem* RuntimeSubsystem = World->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			RuntimeSubsystem->OnCheckoutCompleted.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleCheckoutCompleted);
			RuntimeSubsystem->OnCheckoutAbandoned.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleCheckoutAbandoned);
			RuntimeSubsystem->OnCustomerDespawned.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleCustomerDespawned);
			RuntimeSubsystem->OnPurchaseRejected.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandlePurchaseRejected);
		}

		if (ACSStoreGameState* GameState = World->GetGameState<ACSStoreGameState>())
		{
			GameState->OnDailySummaryRequested.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleDailySummaryRequested);
			GameState->OnBusinessDayStarted.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleBusinessDayStarted);
		}
	}

	Super::Deinitialize();
}

void UCSStoreReputationSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSReputationDailySaveRecord& Record = OutSave.ReputationDaily;
	Record.DailyEnteredCustomers = DailyEnteredCustomers;
	Record.DailyNormalCheckouts = DailyNormalCheckouts;
	Record.DailyCheckoutAbandoned = DailyCheckoutAbandoned;
	Record.DailyEmptyHandedDepartures = DailyEmptyHandedDepartures;
	Record.DailyPriceUpsetSum = DailyPriceUpsetSum;
	Record.DailyTrashWitnessed = DailyTrashWitnessed;
}

void UCSStoreReputationSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	const FCSReputationDailySaveRecord& Record = InSave.ReputationDaily;
	DailyEnteredCustomers = Record.DailyEnteredCustomers;
	DailyNormalCheckouts = Record.DailyNormalCheckouts;
	DailyCheckoutAbandoned = Record.DailyCheckoutAbandoned;
	DailyEmptyHandedDepartures = Record.DailyEmptyHandedDepartures;
	DailyPriceUpsetSum = Record.DailyPriceUpsetSum;
	DailyTrashWitnessed = Record.DailyTrashWitnessed;

	// 실시간 정산창(C)이 복원된 카운터로 예상 평판 변화를 다시 계산하도록 알림
	OnReputationStatsChanged.Broadcast();
}

void UCSStoreReputationSubsystem::HandleCheckoutCompleted(ACSCustomerCharacter* Customer)
{
	++DailyNormalCheckouts;
	OnReputationStatsChanged.Broadcast();
}

void UCSStoreReputationSubsystem::HandleCheckoutAbandoned(ACSCustomerCharacter* Customer, float WaitedSeconds)
{
	++DailyCheckoutAbandoned;
	OnReputationStatsChanged.Broadcast();
}

void UCSStoreReputationSubsystem::HandleCustomerDespawned(ACSCustomerCharacter* Customer, bool bCheckoutDone)
{
	const UCSCustomerStateComponent* State = IsValid(Customer) ? Customer->GetCustomerState() : nullptr;
	if (State == nullptr)
	{
		return;
	}

	if (!State->bHasEnteredStore)
	{
		// 가게 앞만 지나간 행인은 통계 대상이 아님
		return;
	}


	// 결제도 포기도 안 한 채 마감으로 끊긴 손님은 판단할 근거가 없어 통째로 뺀다
	// 결제나 포기를 한 손님은 강제 퇴장이어도 이미 결과가 집계돼 있어서 분모에 넣어야 한다
	if (State->bForcedToLeave && !State->bCheckoutDone && !State->bCheckoutAbandoned)
	{
		return;
	}

	++DailyEnteredCustomers;

	// 정상 결제와 계산 포기는 그 순간에 이미 셌다. 나머지는 살 게 없어 빈손으로 나간 손님
	if (!State->bCheckoutDone && !State->bCheckoutAbandoned)
	{
		++DailyEmptyHandedDepartures;
	}

	DailyPriceUpsetSum += ComputePriceUpsetWeight(State->MaxPriceUpsetSeverity);

	if (State->bTrashWitnessedToday)
	{
		++DailyTrashWitnessed;
	}

	OnReputationStatsChanged.Broadcast();
}

void UCSStoreReputationSubsystem::HandlePurchaseRejected(ACSCustomerCharacter* Customer, FCSPurchaseRejectionInfo Info)
{
	// 이 델리게이트는 품절/예산초과/카테고리막힘 사유도 같이 태우므로 가격 불쾌가 아니면 무시
	if (Info.Reason != ECSPurchaseRejectReason::PriceRejected)
	{
		return;
	}

	if (UCSCustomerStateComponent* State = IsValid(Customer) ? Customer->GetCustomerState() : nullptr)
	{
		State->MaxPriceUpsetSeverity = FMath::Max(State->MaxPriceUpsetSeverity, Info.Severity);
	}
}

void UCSStoreReputationSubsystem::HandleDailySummaryRequested()
{
	SettleDailyReputation();
}

void UCSStoreReputationSubsystem::HandleBusinessDayStarted(int32 NewBusinessDay)
{
	ResetDailyCounters();
}

void UCSStoreReputationSubsystem::SettleDailyReputation() const
{
	if (DailyEnteredCustomers == 0)
	{
		// 입장 손님이 없던 날 : 0으로 나누기 방지, 정산하지 않고 넘어감
		// 로그를 남기는 이유: 집계가 스킵된 건지 애초에 이 함수가 안 불린 건지 구분하려고
		UE_LOG(LogTemp, Warning, TEXT("[평판 정산] 입장 손님이 없어 정산을 건너뜀"));
		return;
	}

	const float Satisfaction = ComputeSatisfaction();
	const int32 RoundedDelta = ComputeReputationDelta();

	UWorld* World = GetWorld();
	ACSStoreGameState* GameState = World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(GameState))
	{
		UE_LOG(LogTemp, Warning, TEXT("[평판 정산] ACSStoreGameState를 찾을 수 없어 정산을 건너뜀"));
		return;
	}

	GameState->AddReputation(RoundedDelta,
		FString::Printf(TEXT("일일 정산(만족도 %.3f)"), Satisfaction));

	UE_LOG(LogTemp, Warning,
		TEXT("[평판 정산] 입장=%d 정상결제=%d 계산포기=%d 빈손퇴장=%d 가격불쾌=%.3f 쓰레기목격=%d 만족도=%.3f 델타=%d"),
		DailyEnteredCustomers, DailyNormalCheckouts, DailyCheckoutAbandoned, DailyEmptyHandedDepartures,
		DailyPriceUpsetSum, DailyTrashWitnessed, Satisfaction, RoundedDelta);
}

float UCSStoreReputationSubsystem::ComputeSatisfaction() const
{
	return (static_cast<float>(DailyNormalCheckouts)
			- CheckoutAbandonedWeight * DailyCheckoutAbandoned
			- EmptyHandedWeight * DailyEmptyHandedDepartures
			- PriceUpsetWeight * DailyPriceUpsetSum
			- TrashWitnessWeight * DailyTrashWitnessed)
		/ static_cast<float>(DailyEnteredCustomers);
}

int32 UCSStoreReputationSubsystem::ComputeReputationDelta() const
{
	const float Diff = ComputeSatisfaction() - SatisfactionBaseline;
	const float ClampedDelta = Diff >= 0.0f
		? FMath::Min(Diff * ReputationGainScale, MaxDailyReputationGain)
		: FMath::Max(Diff * ReputationLossScale, -MaxDailyReputationLoss);
	return FMath::RoundToInt(ClampedDelta);
}

int32 UCSStoreReputationSubsystem::GetProjectedReputationDelta() const
{
	// 마감 전에는 SettleDailyReputation과 같은 이유(0 나누기 방지)로 아직 계산할 수 없음
	return DailyEnteredCustomers == 0 ? 0 : ComputeReputationDelta();
}

void UCSStoreReputationSubsystem::ResetDailyCounters()
{
	DailyEnteredCustomers = 0;
	DailyNormalCheckouts = 0;
	DailyCheckoutAbandoned = 0;
	DailyEmptyHandedDepartures = 0;
	DailyPriceUpsetSum = 0.0f;
	DailyTrashWitnessed = 0;
}

float UCSStoreReputationSubsystem::ComputePriceUpsetWeight(float Severity) const
{
	if (Severity < PriceUpsetLowThreshold)
	{
		return 0.0f;
	}

	if (Severity < PriceUpsetMidThreshold)
	{
		return PriceUpsetMidWeight * (Severity - PriceUpsetLowThreshold) / (PriceUpsetMidThreshold - PriceUpsetLowThreshold);
	}

	const float HighRatio = FMath::Clamp((Severity - PriceUpsetMidThreshold) / PriceUpsetHighSpan, 0.0f, 1.0f);
	return PriceUpsetMidWeight + PriceUpsetHighWeight * HighRatio;
}
