#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"

#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Features/Delivery/CSDeliveryCartActor.h"
#include "Features/Delivery/CSDeliveryDestinationActor.h"
#include "Features/Delivery/CSDeliveryOrderTerminalActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

namespace
{
bool IsOrderInProgress(const ECSDeliveryOrderStatus Status)
{
	return Status == ECSDeliveryOrderStatus::PendingAcceptance || Status == ECSDeliveryOrderStatus::Accepted;
}
}

void UCSDeliveryOrderSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshBusinessDayState();
	ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState)) return;

	const float CurrentMinutes = StoreGameState->CurrentGameMinutes;
	for (int32 Index = DeliveryOrders.Num() - 1; Index >= 0; --Index)
	{
		const FCSDeliveryOrderRuntime& Order = DeliveryOrders[Index];
		if (Order.Status == ECSDeliveryOrderStatus::PendingAcceptance && CurrentMinutes >= Order.AcceptanceDeadlineMinutes)
		{
			RemoveOrderAt(Index, ECSDeliveryOrderStatus::Expired);
		}
		else if (Order.Status == ECSDeliveryOrderStatus::Accepted &&
			(CurrentMinutes >= Order.DeliveryDeadlineMinutes || !IsStoreOperating()))
		{
			const FCSDeliveryOrderRuntime FailedOrder = Order;
			const bool bDeadlineExpired = CurrentMinutes >= Order.DeliveryDeadlineMinutes;
			StoreGameState->AddReputation(-2);
			RemoveOrderAt(Index, ECSDeliveryOrderStatus::Failed);
			if (bDeadlineExpired)
			{
				ShowDeadlineExpiredSettlement(FailedOrder);
			}
		}
	}

	if (!IsStoreOperating() || GeneratedOrderCountToday >= GetDailyOrderLimit()) return;
	if (NextOrderGenerationMinutes < 0.0f) ScheduleNextOrder(CurrentMinutes);
	if (CurrentMinutes >= NextOrderGenerationMinutes)
	{
		FText IgnoredMessage;
		if (!TryGenerateRandomOrder(IgnoredMessage)) ScheduleNextOrder(CurrentMinutes);
	}
}

TStatId UCSDeliveryOrderSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCSDeliveryOrderSubsystem, STATGROUP_Tickables);
}

bool UCSDeliveryOrderSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FCSDeliveryOrderRuntime UCSDeliveryOrderSubsystem::GetCurrentOrder() const
{
	const int32 Index = FindPrimaryOrderIndex();
	return DeliveryOrders.IsValidIndex(Index) ? DeliveryOrders[Index] : FCSDeliveryOrderRuntime();
}

bool UCSDeliveryOrderSubsystem::HasActiveOrder() const
{
	return !DeliveryOrders.IsEmpty();
}

int32 UCSDeliveryOrderSubsystem::GetAcceptedOrderCount() const
{
	int32 AcceptedOrderCount = 0;
	for (const FCSDeliveryOrderRuntime& Order : DeliveryOrders)
	{
		if (Order.Status == ECSDeliveryOrderStatus::Accepted)
		{
			++AcceptedOrderCount;
		}
	}
	return AcceptedOrderCount;
}

bool UCSDeliveryOrderSubsystem::GetDeliveryOrderById(const int32 OrderId, FCSDeliveryOrderRuntime& OutOrder) const
{
	const FCSDeliveryOrderRuntime* Order = FindOrder(OrderId);
	OutOrder = Order == nullptr ? FCSDeliveryOrderRuntime() : *Order;
	return Order != nullptr;
}

bool UCSDeliveryOrderSubsystem::CanAcceptCurrentOrder(FText& OutReason) const
{
	const int32 Index = FindPrimaryOrderIndex();
	return DeliveryOrders.IsValidIndex(Index) ? CanAcceptOrder(DeliveryOrders[Index].OrderId, OutReason) : CanAcceptOrder(INDEX_NONE, OutReason);
}

bool UCSDeliveryOrderSubsystem::CanAcceptOrder(const int32 OrderId, FText& OutReason) const
{
	const FCSDeliveryOrderRuntime* Order = FindOrder(OrderId);
	if (Order == nullptr || Order->Status != ECSDeliveryOrderStatus::PendingAcceptance)
	{
		OutReason = NSLOCTEXT("Delivery", "NoPendingOrder", "수락할 배달 주문이 없습니다.");
		return false;
	}
	for (const FCSDeliveryOrderLine& Line : Order->RequiredProducts)
	{
		const int32 AvailableQuantity = GetAvailableQuantityForOrder(Line.ProductRow);
		if (AvailableQuantity < Line.Quantity)
		{
			const FCSProductDataRow* ProductData = Line.ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSDeliveryOrderSubsystem::CanAcceptOrder"));
			OutReason = FText::Format(
				NSLOCTEXT("Delivery", "InsufficientStock", "재고가 부족합니다. {0}: 필요 {1}개 / 가능 {2}개"),
				ProductData == nullptr ? NSLOCTEXT("Delivery", "UnknownProduct", "상품") : ProductData->DisplayName,
				FText::AsNumber(Line.Quantity), FText::AsNumber(AvailableQuantity));
			return false;
		}
	}
	OutReason = FText::GetEmpty();
	return true;
}

bool UCSDeliveryOrderSubsystem::AcceptCurrentOrder(FText& OutMessage)
{
	const int32 Index = FindPrimaryOrderIndex();
	return DeliveryOrders.IsValidIndex(Index) ? AcceptOrder(DeliveryOrders[Index].OrderId, OutMessage) : AcceptOrder(INDEX_NONE, OutMessage);
}

bool UCSDeliveryOrderSubsystem::AcceptOrder(const int32 OrderId, FText& OutMessage)
{
	if (!CanAcceptOrder(OrderId, OutMessage)) return false;
	FCSDeliveryOrderRuntime* Order = FindOrderMutable(OrderId);
	if (Order == nullptr) return false;
	ReserveOrderStock(*Order);
	if (ACSStoreGameState* StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>(); IsValid(StoreGameState))
	{
		// 주문을 실제로 수락한 시점부터 배달 제한 시간을 계산합니다.
		Order->DeliveryDeadlineMinutes = FMath::Min(StoreGameState->AutomaticClosingTimeMinutes, StoreGameState->CurrentGameMinutes + DeliveryWindowMinutes);
	}
	Order->Status = ECSDeliveryOrderStatus::Accepted;
	if (IsValid(Order->Destination)) Order->Destination->SetDeliveryOrderHighlight(true, DeliveryOutlineStencilValue);
	BroadcastOrdersChanged(*Order);
	OutMessage = NSLOCTEXT("Delivery", "OrderAccepted", "배달 주문을 수락했습니다.");
	return true;
}

bool UCSDeliveryOrderSubsystem::DeclineCurrentOrder(FText& OutMessage)
{
	const int32 Index = FindPrimaryOrderIndex();
	return DeliveryOrders.IsValidIndex(Index) ? DeclineOrder(DeliveryOrders[Index].OrderId, OutMessage) : DeclineOrder(INDEX_NONE, OutMessage);
}

bool UCSDeliveryOrderSubsystem::DeclineOrder(const int32 OrderId, FText& OutMessage)
{
	const int32 Index = FindOrderIndex(OrderId);
	if (!DeliveryOrders.IsValidIndex(Index) || DeliveryOrders[Index].Status != ECSDeliveryOrderStatus::PendingAcceptance)
	{
		OutMessage = NSLOCTEXT("Delivery", "NoOrderToDecline", "거절할 주문이 없습니다.");
		return false;
	}
	RemoveOrderAt(Index, ECSDeliveryOrderStatus::Declined);
	OutMessage = NSLOCTEXT("Delivery", "OrderDeclined", "배달 주문을 거절했습니다.");
	return true;
}

bool UCSDeliveryOrderSubsystem::CancelCurrentOrder(FText& OutMessage)
{
	const int32 Index = FindPrimaryOrderIndex();
	return DeliveryOrders.IsValidIndex(Index) ? CancelOrder(DeliveryOrders[Index].OrderId, OutMessage) : CancelOrder(INDEX_NONE, OutMessage);
}

bool UCSDeliveryOrderSubsystem::CancelOrder(const int32 OrderId, FText& OutMessage)
{
	const int32 Index = FindOrderIndex(OrderId);
	if (!DeliveryOrders.IsValidIndex(Index) || DeliveryOrders[Index].Status != ECSDeliveryOrderStatus::Accepted)
	{
		OutMessage = NSLOCTEXT("Delivery", "NoAcceptedOrder", "취소할 진행 중 배달 주문이 없습니다.");
		return false;
	}
	if (ACSStoreGameState* StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>(); IsValid(StoreGameState)) StoreGameState->AddReputation(-1);
	RemoveOrderAt(Index, ECSDeliveryOrderStatus::Canceled);
	OutMessage = NSLOCTEXT("Delivery", "OrderCanceled", "배달 주문을 취소했습니다. 예약 재고가 해제되고 평판이 1 감소했습니다.");
	return true;
}

bool UCSDeliveryOrderSubsystem::TryCompleteCurrentOrder(ACSDeliveryDestinationActor* Destination, ACSDeliveryCartActor* DeliveryCart, FText& OutMessage)
{
	const int32 OrderIndex = DeliveryOrders.IndexOfByPredicate([Destination](const FCSDeliveryOrderRuntime& Order)
	{
		return Order.Status == ECSDeliveryOrderStatus::Accepted && Order.Destination == Destination;
	});
	if (!DeliveryOrders.IsValidIndex(OrderIndex))
	{
		OutMessage = NSLOCTEXT("Delivery", "NoOrderAtDestination", "이 배달 지점에 진행 중인 주문이 없습니다.");
		return false;
	}
	if (!IsValid(DeliveryCart))
	{
		OutMessage = NSLOCTEXT("Delivery", "CartRequired", "배달 수레를 배달 지점 안으로 가져오세요.");
		return false;
	}

	const FCSDeliveryOrderRuntime& Order = DeliveryOrders[OrderIndex];
	const TArray<ACSProductPickupActor*> AvailableProducts = DeliveryCart->GetCargoProducts();
	TArray<ACSProductPickupActor*> ProductsToDeliver;
	TSet<ACSProductPickupActor*> UsedProducts;
	for (const FCSDeliveryOrderLine& OrderLine : Order.RequiredProducts)
	{
		int32 FoundCount = 0;
		for (ACSProductPickupActor* Product : AvailableProducts)
		{
			if (IsValid(Product) && !UsedProducts.Contains(Product) && Product->GetProductRow() == OrderLine.ProductRow)
			{
				ProductsToDeliver.Add(Product);
				UsedProducts.Add(Product);
				if (++FoundCount >= OrderLine.Quantity) break;
			}
		}
		if (FoundCount < OrderLine.Quantity)
		{
			OutMessage = NSLOCTEXT("Delivery", "MissingCargoProducts", "수레 적재함에 주문 상품이 부족합니다.");
			return false;
		}
	}

	for (ACSProductPickupActor* Product : ProductsToDeliver)
	{
		DeliveryCart->RemoveCargoProduct(Product);
		Product->Destroy();
	}
	if (ACSStoreGameState* StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>(); IsValid(StoreGameState))
	{
		StoreGameState->AddFunds(Order.RewardGold);
		StoreGameState->AddReputation(Order.ReputationReward);
	}
	const FText CompletionText = FText::Format(NSLOCTEXT("Delivery", "DeliverySuccess", "배달을 완료했습니다. +{0} 골드 / 평판 +{1}"), FText::AsNumber(Order.RewardGold), FText::AsNumber(Order.ReputationReward));
	RemoveOrderAt(OrderIndex, ECSDeliveryOrderStatus::Completed);
	OutMessage = CompletionText;
	return true;
}

int32 UCSDeliveryOrderSubsystem::GetReservedQuantity(const FName ProductId) const
{
	return ProductId.IsNone() ? 0 : ReservedQuantities.FindRef(ProductId);
}

int32 UCSDeliveryOrderSubsystem::GetAvailableQuantityForOrder(const FDataTableRowHandle& ProductRow) const
{
	if (ProductRow.RowName.IsNone() || GetWorld() == nullptr) return 0;
	const UCSStoreStockSubsystem* StockSubsystem = GetWorld()->GetSubsystem<UCSStoreStockSubsystem>();
	if (!IsValid(StockSubsystem)) return 0;
	return FMath::Max(0, StockSubsystem->GetProductStock(ProductRow.RowName).TotalQuantity - GetReservedQuantity(ProductRow.RowName));
}

bool UCSDeliveryOrderSubsystem::GenerateOrderNow(FText& OutMessage)
{
	RefreshBusinessDayState();
	return TryGenerateRandomOrder(OutMessage);
}

bool UCSDeliveryOrderSubsystem::TryGenerateRandomOrder(FText& OutMessage)
{
	if (GetWorld() == nullptr)
	{
		OutMessage = NSLOCTEXT("Delivery", "NoWorld", "월드를 찾을 수 없습니다.");
		return false;
	}
	if (GeneratedOrderCountToday >= GetDailyOrderLimit())
	{
		OutMessage = NSLOCTEXT("Delivery", "DailyOrderLimit", "오늘 생성할 수 있는 배달 주문 수를 모두 사용했습니다.");
		return false;
	}

	ACSDeliveryOrderTerminalActor* OrderTerminal = nullptr;
	for (TActorIterator<ACSDeliveryOrderTerminalActor> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && (*It)->GetProductDataTable() != nullptr) { OrderTerminal = *It; break; }
	}
	if (!IsValid(OrderTerminal))
	{
		OutMessage = NSLOCTEXT("Delivery", "NoOrderTerminal", "상품 데이터 테이블이 설정된 배달 주문 단말기가 없습니다.");
		return false;
	}

	TArray<ACSDeliveryDestinationActor*> Destinations;
	for (TActorIterator<ACSDeliveryDestinationActor> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && (*It)->CanReceiveRandomDeliveryOrder()) Destinations.Add(*It);
	}
	if (Destinations.IsEmpty())
	{
		OutMessage = NSLOCTEXT("Delivery", "NoDestination", "주문에 배정할 수 있는 빈 배달 지점이 없습니다.");
		return false;
	}

	ACSStoreGameState* StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState))
	{
		OutMessage = NSLOCTEXT("Delivery", "NoStoreGameState", "상점 상태를 찾을 수 없습니다.");
		return false;
	}

	TArray<FName> EligibleRowNames;
	TArray<FName> InStockRowNames;
	for (const FName RowName : OrderTerminal->GetProductDataTable()->GetRowNames())
	{
		const FCSProductDataRow* ProductData = OrderTerminal->GetProductDataTable()->FindRow<FCSProductDataRow>(RowName, TEXT("UCSDeliveryOrderSubsystem::TryGenerateRandomOrder"));
		if (ProductData == nullptr || !StoreGameState->IsStoreLevelRequirementMet(ProductData->StoreLevelUnlockCondition) || !StoreGameState->IsReputationRequirementMet(ProductData->ReputationUnlockCondition)) continue;
		EligibleRowNames.Add(RowName);
		FDataTableRowHandle ProductRow;
		ProductRow.DataTable = OrderTerminal->GetProductDataTable();
		ProductRow.RowName = RowName;
		if (GetAvailableQuantityForOrder(ProductRow) > 0) InStockRowNames.Add(RowName);
	}
	if (EligibleRowNames.IsEmpty())
	{
		OutMessage = NSLOCTEXT("Delivery", "NoEligibleProduct", "배달 주문에 사용할 수 있는 상품이 없습니다.");
		return false;
	}

	FCSDeliveryOrderRuntime NewOrder;
	NewOrder.OrderId = NextOrderId++;
	NewOrder.Status = ECSDeliveryOrderStatus::PendingAcceptance;
	NewOrder.Destination = Destinations[FMath::RandRange(0, Destinations.Num() - 1)];
	TArray<FName> CandidateRows = EligibleRowNames;
	const int32 LineCount = FMath::Min(FMath::RandRange(1, GetMaximumLineCount()), CandidateRows.Num());
	for (int32 LineIndex = 0; LineIndex < LineCount; ++LineIndex)
	{
		const bool bPreferStock = !InStockRowNames.IsEmpty() && FMath::FRand() < InStockProductPreference;
		TArray<FName> PreferredRows;
		if (bPreferStock)
		{
			for (const FName RowName : CandidateRows) if (InStockRowNames.Contains(RowName)) PreferredRows.Add(RowName);
		}
		const TArray<FName>& SourceRows = PreferredRows.IsEmpty() ? CandidateRows : PreferredRows;
		const FName SelectedRow = SourceRows[FMath::RandRange(0, SourceRows.Num() - 1)];
		CandidateRows.Remove(SelectedRow);
		FCSDeliveryOrderLine& NewLine = NewOrder.RequiredProducts.AddDefaulted_GetRef();
		NewLine.ProductRow.DataTable = OrderTerminal->GetProductDataTable();
		NewLine.ProductRow.RowName = SelectedRow;
		NewLine.Quantity = FMath::RandRange(MinimumProductQuantity, MaximumProductQuantity);
		if (const FCSProductDataRow* ProductData = NewLine.ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSDeliveryOrderSubsystem::TryGenerateRandomOrder"))) NewOrder.RewardGold += ProductData->RecommendedSalePrice * NewLine.Quantity;
	}
	NewOrder.RewardGold = FMath::Max(100, FMath::RoundToInt(NewOrder.RewardGold * 1.3f));
	NewOrder.ReputationReward = FMath::Clamp(NewOrder.RequiredProducts.Num(), 1, 3);
	NewOrder.AcceptanceDeadlineMinutes = FMath::Min(StoreGameState->AutomaticClosingTimeMinutes, StoreGameState->CurrentGameMinutes + AcceptanceWindowMinutes);
	NewOrder.Destination->AssignDeliveryOrder(NewOrder.OrderId);
	DeliveryOrders.Add(NewOrder);
	++GeneratedOrderCountToday;
	NextOrderGenerationMinutes = -1.0f;
	BroadcastOrdersChanged(NewOrder);
	OutMessage = NSLOCTEXT("Delivery", "OrderGenerated", "새 배달 주문이 도착했습니다.");
	return true;
}

bool UCSDeliveryOrderSubsystem::IsStoreOperating() const
{
	const ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	return IsValid(StoreGameState) && StoreGameState->CurrentPhase == ECStorePhase::Open && StoreGameState->bIsStoreOpen && StoreGameState->bIsGameClockRunning;
}

void UCSDeliveryOrderSubsystem::RefreshBusinessDayState()
{
	const ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState) || LastObservedBusinessDay == StoreGameState->CurrentBusinessDay) return;
	LastObservedBusinessDay = StoreGameState->CurrentBusinessDay;
	GeneratedOrderCountToday = 0;
	NextOrderGenerationMinutes = -1.0f;
	for (int32 Index = DeliveryOrders.Num() - 1; Index >= 0; --Index) RemoveOrderAt(Index, ECSDeliveryOrderStatus::Failed);
}

void UCSDeliveryOrderSubsystem::ScheduleNextOrder(const float CurrentGameMinutes)
{
	NextOrderGenerationMinutes = CurrentGameMinutes + FMath::FRandRange(MinimumOrderIntervalMinutes, MaximumOrderIntervalMinutes);
}

int32 UCSDeliveryOrderSubsystem::GetDailyOrderLimit() const
{
	const ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState)) return 0;
	switch (StoreGameState->GetReputationGrade())
	{
	case ECSReputationGrade::Excellent: return MaxOrdersExcellent;
	case ECSReputationGrade::Good: return MaxOrdersGood;
	case ECSReputationGrade::Normal: return MaxOrdersNormal;
	default: return MaxOrdersVeryBadOrBad;
	}
}

int32 UCSDeliveryOrderSubsystem::GetMaximumLineCount() const
{
	const ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState)) return 1;
	switch (StoreGameState->GetReputationGrade())
	{
	case ECSReputationGrade::Excellent: return 3;
	case ECSReputationGrade::Good: return 2;
	default: return 1;
	}
}

int32 UCSDeliveryOrderSubsystem::FindOrderIndex(const int32 OrderId) const
{
	return DeliveryOrders.IndexOfByPredicate([OrderId](const FCSDeliveryOrderRuntime& Order) { return Order.OrderId == OrderId; });
}

FCSDeliveryOrderRuntime* UCSDeliveryOrderSubsystem::FindOrderMutable(const int32 OrderId)
{
	const int32 Index = FindOrderIndex(OrderId);
	return DeliveryOrders.IsValidIndex(Index) ? &DeliveryOrders[Index] : nullptr;
}

const FCSDeliveryOrderRuntime* UCSDeliveryOrderSubsystem::FindOrder(const int32 OrderId) const
{
	const int32 Index = FindOrderIndex(OrderId);
	return DeliveryOrders.IsValidIndex(Index) ? &DeliveryOrders[Index] : nullptr;
}

int32 UCSDeliveryOrderSubsystem::FindPrimaryOrderIndex() const
{
	const int32 PendingIndex = DeliveryOrders.IndexOfByPredicate([](const FCSDeliveryOrderRuntime& Order) { return Order.Status == ECSDeliveryOrderStatus::PendingAcceptance; });
	return PendingIndex != INDEX_NONE ? PendingIndex : (DeliveryOrders.IsEmpty() ? INDEX_NONE : 0);
}

void UCSDeliveryOrderSubsystem::ReserveOrderStock(const FCSDeliveryOrderRuntime& Order)
{
	for (const FCSDeliveryOrderLine& Line : Order.RequiredProducts) if (!Line.ProductRow.RowName.IsNone()) ReservedQuantities.FindOrAdd(Line.ProductRow.RowName) += Line.Quantity;
}

void UCSDeliveryOrderSubsystem::ReleaseOrderReservation(const FCSDeliveryOrderRuntime& Order)
{
	for (const FCSDeliveryOrderLine& Line : Order.RequiredProducts)
	{
		if (int32* ReservedQuantity = ReservedQuantities.Find(Line.ProductRow.RowName))
		{
			*ReservedQuantity = FMath::Max(0, *ReservedQuantity - Line.Quantity);
			if (*ReservedQuantity == 0) ReservedQuantities.Remove(Line.ProductRow.RowName);
		}
	}
}

void UCSDeliveryOrderSubsystem::ClearDestinationAssignment(const FCSDeliveryOrderRuntime& Order)
{
	if (IsValid(Order.Destination))
	{
		Order.Destination->SetDeliveryOrderHighlight(false, DeliveryOutlineStencilValue);
		Order.Destination->ClearDeliveryOrder(Order.OrderId);
	}
}

void UCSDeliveryOrderSubsystem::BroadcastOrdersChanged(const FCSDeliveryOrderRuntime& ChangedOrder)
{
	OnDeliveryOrderChanged.Broadcast(ChangedOrder);
	OnDeliveryOrdersChanged.Broadcast();
}

void UCSDeliveryOrderSubsystem::RemoveOrderAt(const int32 OrderIndex, const ECSDeliveryOrderStatus FinalStatus)
{
	if (!DeliveryOrders.IsValidIndex(OrderIndex)) return;
	const FCSDeliveryOrderRuntime PreviousOrder = DeliveryOrders[OrderIndex];
	FCSDeliveryOrderRuntime FinishedOrder = PreviousOrder;
	FinishedOrder.Status = FinalStatus;
	if (PreviousOrder.Status == ECSDeliveryOrderStatus::Accepted) ReleaseOrderReservation(PreviousOrder);
	ClearDestinationAssignment(FinishedOrder);
	DeliveryOrders.RemoveAt(OrderIndex);
	BroadcastOrdersChanged(FinishedOrder);
}

void UCSDeliveryOrderSubsystem::ShowDeadlineExpiredSettlement(
	const FCSDeliveryOrderRuntime& ExpiredOrder)
{
	ACSStorePlayerController* StorePlayerController = GetWorld() == nullptr
		? nullptr
		: Cast<ACSStorePlayerController>(GetWorld()->GetFirstPlayerController());
	if (!IsValid(StorePlayerController) || !IsValid(ExpiredOrder.Destination))
	{
		return;
	}

	// 기한 초과는 자동 발생하므로 입력을 막지 않는 전용 실패 정산 UI로 안내합니다.
	ExpiredOrder.Destination->ShowDeliverySettlementForPlayer(
		StorePlayerController,
		NSLOCTEXT("Delivery", "DeliveryDeadlineExpired", "배달 기한을 초과했습니다. 평판이 2 감소했습니다."),
		ECSDeliverySettlementResult::DeadlineExpired);
}

bool UCSDeliveryOrderSubsystem::IsOrderAtDestination(const FCSDeliveryOrderRuntime& Order, const ACSDeliveryDestinationActor* Destination) const
{
	return IsValid(Destination) && Order.Destination == Destination;
}
