#include "Systems/StoreFlow/CSStoreGameState.h"

void ACSStoreGameState::BeginPlay()
{
	Super::BeginPlay();

	CurrentFunds = FMath::Max(0, StartingFunds);
	OnFundsChanged.Broadcast(CurrentFunds);
}

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

	CurrentFunds -= Amount;
	OnFundsChanged.Broadcast(CurrentFunds);
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

	OnCheckoutRecorded.Broadcast(Transaction);
	OnDailySalesChanged.Broadcast();
	return true;
}

void ACSStoreGameState::BeginNextBusinessDay()
{
	++CurrentBusinessDay;
	DailySalesGold = 0;
	DailyCostGold = 0;
	DailyProfitGold = 0;
	DailySoldQuantity = 0;
	CompletedCheckoutCount = 0;
	DailyCheckoutTransactions.Reset();
	NextTransactionId = 1;

	// 다음 날 HUD와 정산 화면이 이전 날의 값을 유지하지 않도록 갱신을 알립니다.
	OnDailySalesChanged.Broadcast();
	// 월드에 남아 있는 상품이 하루 경과에 따른 변질 상태를 갱신할 수 있도록 알립니다.
	OnBusinessDayStarted.Broadcast(CurrentBusinessDay);
}
