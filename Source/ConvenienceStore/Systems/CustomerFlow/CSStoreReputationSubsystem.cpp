#include "Systems/CustomerFlow/CSStoreReputationSubsystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"

// 손님별 스트림과 별개로 이 서브시스템도 같은 시드에서 독립적으로 초기화해
// "매장 공용, 손님 무관" 패턴을 따른다
extern TAutoConsoleVariable<int32> CVarCS_FixedSeed;

void UCSStoreReputationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const int32 FixedSeed = CVarCS_FixedSeed.GetValueOnGameThread();
	ReputationRollStream.Initialize(FixedSeed >= 0 ? FixedSeed : FMath::Rand());

	if (UCSCustomerRuntimeSubsystem* RuntimeSubsystem = InWorld.GetSubsystem<UCSCustomerRuntimeSubsystem>())
	{
		RuntimeSubsystem->OnCheckoutCompleted.AddDynamic(this, &UCSStoreReputationSubsystem::HandleCheckoutCompleted);
		RuntimeSubsystem->OnCheckoutAbandoned.AddDynamic(this, &UCSStoreReputationSubsystem::HandleCheckoutAbandoned);
		RuntimeSubsystem->OnCustomerDespawned.AddDynamic(this, &UCSStoreReputationSubsystem::HandleCustomerDespawned);
		RuntimeSubsystem->OnPurchaseRejected.AddDynamic(this, &UCSStoreReputationSubsystem::HandlePurchaseRejected);
		RuntimeSubsystem->OnTrashWitnessed.AddDynamic(this, &UCSStoreReputationSubsystem::HandleTrashWitnessed);
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
			RuntimeSubsystem->OnTrashWitnessed.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleTrashWitnessed);
		}

		if (ACSStoreGameState* GameState = World->GetGameState<ACSStoreGameState>())
		{
			GameState->OnDailySummaryRequested.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleDailySummaryRequested);
			GameState->OnBusinessDayStarted.RemoveDynamic(this, &UCSStoreReputationSubsystem::HandleBusinessDayStarted);
		}
	}

	Super::Deinitialize();
}

void UCSStoreReputationSubsystem::HandleCheckoutCompleted(ACSCustomerCharacter* Customer)
{
	// 정상 결제는 추첨 없이 그대로 카운트
	++DailyNormalCheckouts;
	OnReputationStatsChanged.Broadcast();
}

void UCSStoreReputationSubsystem::HandleCheckoutAbandoned(ACSCustomerCharacter* Customer, float WaitedSeconds)
{
	if (RollShouldCount())
	{
		++DailyCheckoutAbandoned;
		OnReputationStatsChanged.Broadcast();
	}
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


	if (State->bCheckoutDone)
	{
		// 정상 결제는 HandleCheckoutCompleted에서 이미 집계됨. 입장 집계에만 반영
		++DailyEnteredCustomers;
		OnReputationStatsChanged.Broadcast();
		return;
	}

	if (State->bCheckoutAbandoned)
	{
		// 계산 포기는 HandleCheckoutAbandoned에서 이미 추첨·집계됨. 입장 집계에만 반영(중복 방지)
		++DailyEnteredCustomers;
		OnReputationStatsChanged.Broadcast();
		return;
	}

	if (State->bForcedToLeave)
	{
		// 결제도 포기도 안 한 채 마감으로 끊긴 진짜 미해결 손님만 분모(DailyEnteredCustomers)에서도 제외
		return;
	}

	++DailyEnteredCustomers;

	// 여기까지 왔으면 살 게 없거나 마음이 바뀌어 빈손으로 나간 진짜 빈손 퇴장
	if (RollShouldCount())
	{
		++DailyEmptyHandedDepartures;
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

	UCSCustomerStateComponent* State = IsValid(Customer) ? Customer->GetCustomerState() : nullptr;
	if (State == nullptr)
	{
		return;
	}

	if (State->bPriceUpsetCounted)
	{
		// 손님 한 명이 하루에 여러 번 가격에 반응해도 평판 감점은 방문당 1회로 제한
		return;
	}

	const float UpsetWeight = ComputePriceUpsetWeight(Info.Severity);

	// 추첨이 먼저인 이유: 순서를 바꾸면 ReputationRollStream 호출 횟수가 달라져 같은 시드 결과가 밀린다
	if (!RollShouldCount())
	{
		return;
	}

	// 감점 0인 약한 반응이 방문당 1회 슬롯을 태우면 이후 진짜 바가지가 감점되지 않는다
	if (UpsetWeight <= 0.0f)
	{
		return;
	}

	DailyPriceUpsetSum += UpsetWeight;
	State->bPriceUpsetCounted = true;
	OnReputationStatsChanged.Broadcast();
}

void UCSStoreReputationSubsystem::HandleTrashWitnessed(ACSCustomerCharacter* Customer)
{
	// 이 델리게이트는 하루 1회 캡(bTrashWitnessedToday)과 30% 목격 확률을
	// CSCustomerRuntimeSubsystem이 이미 통과시킨 뒤에만 들어오므로 그대로 누적한다
	++DailyTrashWitnessed;
	OnReputationStatsChanged.Broadcast();
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
	const float RawDelta = (ComputeSatisfaction() - SatisfactionBaseline) * ReputationDeltaScale;
	const float ClampedDelta = FMath::Clamp(RawDelta, -MaxDailyReputationDelta, MaxDailyReputationDelta);
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

bool UCSStoreReputationSubsystem::RollShouldCount() const
{
	return ReputationRollStream.FRand() < CountRollChance;
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
