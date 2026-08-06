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
		const int32 LineSalesGold = LineItem.UnitSalePrice * LineItem.Quantity;
		const int32 LineCostGold = LineItem.UnitPurchasePrice * LineItem.Quantity;
		Transaction.LineItems[LineIndex].LineTotalSalePrice = LineSalesGold;
		Transaction.TotalSalesGold += LineSalesGold;
		Transaction.TotalCostGold += LineCostGold;
	}

	Transaction.TotalProfitGold = Transaction.TotalSalesGold - Transaction.TotalCostGold;

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
}
