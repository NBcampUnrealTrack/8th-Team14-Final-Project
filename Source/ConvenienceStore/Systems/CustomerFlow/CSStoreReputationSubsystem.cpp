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
}

void UCSStoreReputationSubsystem::HandleCheckoutAbandoned(ACSCustomerCharacter* Customer, float WaitedSeconds)
{
	if (RollShouldCount())
	{
		++DailyCheckoutAbandoned;
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

	++DailyEnteredCustomers;

	if (State->bCheckoutDone)
	{
		// 정상 결제는 HandleCheckoutCompleted에서 이미 집계됨
		return;
	}

	if (State->bCheckoutAbandoned)
	{
		// 계산 포기는 HandleCheckoutAbandoned에서 이미 추첨·집계됨. 중복 방지
		return;
	}

	if (State->bForcedToLeave)
	{
		// 영업종료 강제 퇴장은 플레이어 잘못이 아니므로 감점 대상에서 제외
		return;
	}

	// 여기까지 왔으면 살 게 없거나 마음이 바뀌어 빈손으로 나간 진짜 빈손 퇴장
	if (RollShouldCount())
	{
		++DailyEmptyHandedDepartures;
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

	const float Satisfaction =
		(static_cast<float>(DailyNormalCheckouts)
			- CheckoutAbandonedWeight * DailyCheckoutAbandoned
			- EmptyHandedWeight * DailyEmptyHandedDepartures)
		/ static_cast<float>(DailyEnteredCustomers);

	const float RawDelta = (Satisfaction - SatisfactionBaseline) * ReputationDeltaScale;
	const float ClampedDelta = FMath::Clamp(RawDelta, -MaxDailyReputationDelta, MaxDailyReputationDelta);
	const int32 RoundedDelta = FMath::RoundToInt(ClampedDelta);

	UWorld* World = GetWorld();
	ACSStoreGameState* GameState = World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(GameState))
	{
		UE_LOG(LogTemp, Warning, TEXT("[평판 정산] ACSStoreGameState를 찾을 수 없어 정산을 건너뜀"));
		return;
	}

	GameState->AddReputation(RoundedDelta);

	UE_LOG(LogTemp, Warning,
		TEXT("[평판 정산] 입장=%d 정상결제=%d 계산포기=%d 빈손퇴장=%d 만족도=%.3f 델타=%d"),
		DailyEnteredCustomers, DailyNormalCheckouts, DailyCheckoutAbandoned, DailyEmptyHandedDepartures,
		Satisfaction, RoundedDelta);
}

void UCSStoreReputationSubsystem::ResetDailyCounters()
{
	DailyEnteredCustomers = 0;
	DailyNormalCheckouts = 0;
	DailyCheckoutAbandoned = 0;
	DailyEmptyHandedDepartures = 0;
}

bool UCSStoreReputationSubsystem::RollShouldCount() const
{
	return ReputationRollStream.FRand() < CountRollChance;
}
