#include "Systems/Ordering/CSOrderSubsystem.h"

#include "Data/CSFacilityTypes.h"
#include "Misc/ScopeExit.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Data/CSProductTypes.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Delivery/CSDeliveryPortalActor.h"
#include "Features/Delivery/CSFacilityDeliveryPortalActor.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

namespace
{
	// 첫 구매는 반값이고 하나 살 때마다 기본가의 25%씩 오릅니다. 5개째까지의 합이 기본가 × 5와 같습니다.
	// 초반에 진열대를 늘리기 어렵다는 피드백으로 넣은 값이라 플레이 확인 전입니다.
	constexpr float ShelfFirstPriceRatio = 0.5f;
	constexpr float ShelfPriceStepRatio = 0.25f;
	constexpr float ShelfMaxPriceRatio = 1.75f;
	constexpr int64 ShelfPriceRounding = 50;
}

void UCSOrderSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSOrderSaveRecord& Record = OutSave.Order;

	// 상품 발주는 행과 상자 수를 기록
	Record.PendingProductOrders.Reset(PendingProductOrders.Num());
	for (const FCSPendingProductOrder& PendingOrder : PendingProductOrders)
	{
		FCSPendingOrderRecord OrderRecord;
		OrderRecord.DeliveryBusinessDay = PendingOrder.DeliveryBusinessDay;
		OrderRecord.OrderLines.Reserve(PendingOrder.OrderLines.Num());
		for (const FCSOrderLine& Line : PendingOrder.OrderLines)
		{
			FCSOrderLineRecord LineRecord;
			LineRecord.Row = FCSSaveRowRef::FromHandle(Line.ProductRow);
			LineRecord.Quantity = Line.CrateCount;
			OrderRecord.OrderLines.Add(LineRecord);
		}

		Record.PendingProductOrders.Add(OrderRecord);
	}

	// 시설 발주는 행과 수량을 기록
	Record.PendingFacilityOrders.Reset(PendingFacilityOrders.Num());
	for (const FCSPendingFacilityOrder& PendingOrder : PendingFacilityOrders)
	{
		FCSPendingOrderRecord OrderRecord;
		OrderRecord.DeliveryBusinessDay = PendingOrder.DeliveryBusinessDay;
		OrderRecord.OrderLines.Reserve(PendingOrder.OrderLines.Num());
		for (const FCSFacilityOrderLine& Line : PendingOrder.OrderLines)
		{
			FCSOrderLineRecord LineRecord;
			LineRecord.Row = FCSSaveRowRef::FromHandle(Line.FacilityRow);
			LineRecord.Quantity = Line.Quantity;
			OrderRecord.OrderLines.Add(LineRecord);
		}

		Record.PendingFacilityOrders.Add(OrderRecord);
	}

	// 같은 영업일의 배송 스폰 위치가 처음부터 다시 배정되는 상황을 방지
	Record.DeliverySpawnIndexBusinessDay = DeliverySpawnIndexBusinessDay;
	Record.NextProductDeliverySpawnIndex = NextProductDeliverySpawnIndex;
	Record.NextFacilityDeliverySpawnIndex = NextFacilityDeliverySpawnIndex;
	Record.PurchasedShelfCounts = PurchasedShelfCounts;
}

void UCSOrderSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	const FCSOrderSaveRecord& Record = InSave.Order;

	// 테이블을 해석하지 못한 줄은 배송할 수 없으므로 제외
	PendingProductOrders.Reset(Record.PendingProductOrders.Num());
	for (const FCSPendingOrderRecord& OrderRecord : Record.PendingProductOrders)
	{
		FCSPendingProductOrder PendingOrder;
		PendingOrder.DeliveryBusinessDay = OrderRecord.DeliveryBusinessDay;
		PendingOrder.OrderLines.Reserve(OrderRecord.OrderLines.Num());
		for (const FCSOrderLineRecord& LineRecord : OrderRecord.OrderLines)
		{
			FCSOrderLine Line;
			Line.ProductRow = LineRecord.Row.ToHandle();
			Line.CrateCount = LineRecord.Quantity;
			if (Line.ProductRow.DataTable != nullptr)
			{
				PendingOrder.OrderLines.Add(Line);
			}
		}

		PendingProductOrders.Add(PendingOrder);
	}

	PendingFacilityOrders.Reset(Record.PendingFacilityOrders.Num());
	for (const FCSPendingOrderRecord& OrderRecord : Record.PendingFacilityOrders)
	{
		FCSPendingFacilityOrder PendingOrder;
		PendingOrder.DeliveryBusinessDay = OrderRecord.DeliveryBusinessDay;
		PendingOrder.OrderLines.Reserve(OrderRecord.OrderLines.Num());
		for (const FCSOrderLineRecord& LineRecord : OrderRecord.OrderLines)
		{
			FCSFacilityOrderLine Line;
			Line.FacilityRow = LineRecord.Row.ToHandle();
			Line.Quantity = LineRecord.Quantity;
			if (Line.FacilityRow.DataTable != nullptr)
			{
				PendingOrder.OrderLines.Add(Line);
			}
		}

		PendingFacilityOrders.Add(PendingOrder);
	}

	DeliverySpawnIndexBusinessDay = Record.DeliverySpawnIndexBusinessDay;
	NextProductDeliverySpawnIndex = Record.NextProductDeliverySpawnIndex;
	NextFacilityDeliverySpawnIndex = Record.NextFacilityDeliverySpawnIndex;
	PurchasedShelfCounts = Record.PurchasedShelfCounts;
}

namespace
{
constexpr int32 ImmediateDeliveryFeePercent = 40;

bool TryCalculateDeliveryAdjustedCost(
	int64 BaseLineCost,
	ECSOrderDeliveryType DeliveryType,
	int64& OutFinalLineCost)
{
	if (BaseLineCost < 0 || BaseLineCost > MAX_int32)
	{
		return false;
	}

	if (DeliveryType == ECSOrderDeliveryType::Immediate)
	{
		// int64 정수 연산으로 Line별 1.4배 금액을 일반 반올림합니다.
		OutFinalLineCost =
			(BaseLineCost * (100 + ImmediateDeliveryFeePercent) + 50) / 100;
	}
	else
	{
		OutFinalLineCost = BaseLineCost;
	}

	return OutFinalLineCost <= MAX_int32;
}

void DestroySpawnedActors(const TArray<AActor*>& SpawnedActors)
{
	for (AActor* SpawnedActor : SpawnedActors)
	{
		if (IsValid(SpawnedActor))
		{
			SpawnedActor->Destroy();
		}
	}
}

}

void UCSOrderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TrySubscribeToStoreGameState();
}

void UCSOrderSubsystem::Deinitialize()
{
	UnsubscribeFromStoreGameState();
	PendingProductOrders.Reset();
	PendingFacilityOrders.Reset();
	PurchasedShelfCounts.Reset();
	RegisteredDeliveryPortal.Reset();
	RegisteredFacilityDeliveryPortal.Reset();
	DeliverySpawnIndexBusinessDay = INDEX_NONE;
	NextProductDeliverySpawnIndex = 0;
	NextFacilityDeliverySpawnIndex = 0;

	Super::Deinitialize();
}

bool UCSOrderSubsystem::IsProductUnlocked(const FDataTableRowHandle& Row) const
{
	if (!Row.DataTable || Row.DataTable->GetRowStruct() != FCSProductDataRow::StaticStruct()) return false;
	// 기존 Blueprint 호출 이름은 보존하되 발주 가능 여부만 검사합니다.
	const FCSProductDataRow* Data = Row.GetRow<FCSProductDataRow>(TEXT("Order availability"));
	return Data && Data->bOrderable;
}

bool UCSOrderSubsystem::IsFacilityUnlocked(const FDataTableRowHandle& Row) const
{
	if (!Row.DataTable || Row.DataTable->GetRowStruct() != FCSFacilityDataRow::StaticStruct()) return false;
	return Row.GetRow<FCSFacilityDataRow>(TEXT("Order availability")) != nullptr;
}

TArray<FName> UCSOrderSubsystem::GetUnlockedFacilityRowNames(UDataTable* Table) const
{
	TArray<FName> Names;
	if (Table && Table->GetRowStruct() == FCSFacilityDataRow::StaticStruct())
	{
		for (FName Name : Table->GetRowNames())
		{
			FDataTableRowHandle Row;
			Row.DataTable = Table;
			Row.RowName = Name;
			if (IsFacilityUnlocked(Row)) Names.Add(Name);
		}
	}
	return Names;
}

FCSCombinedOrderResult UCSOrderSubsystem::EvaluateCombinedOrder(
	const TArray<FCSOrderLine>& Products, const TArray<FCSFacilityOrderLine>& Facilities,
	ECSOrderDeliveryType DeliveryType) const
{
	FCSCombinedOrderResult Result;
	Result.DeliveryType = DeliveryType;
	auto Fail = [&Result](ECSCombinedOrderFailure Reason, const FText& Message)
	{
		Result.bSuccess = false;
		Result.Failure = Reason;
		Result.Message = Message;
		return Result;
	};
	const ACSStoreGameState* State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (!State) return Fail(ECSCombinedOrderFailure::Unavailable,
		NSLOCTEXT("Order", "Unavailable", "발주 정보를 불러올 수 없습니다."));
	Result.CurrentFunds = State->GetCurrentFunds();
	Result.RemainingFunds = Result.CurrentFunds;
	for (const auto& Line : Products)
	{
		if (Line.CrateCount != 0 && (!Line.ProductRow.DataTable ||
			Line.ProductRow.DataTable->GetRowStruct() != FCSProductDataRow::StaticStruct()))
			return Fail(ECSCombinedOrderFailure::ProductInvalid,
				NSLOCTEXT("Order", "ProductTable", "상품 데이터 테이블을 확인해 주세요."));
	}
	for (const auto& Line : Facilities)
	{
		if (Line.Quantity != 0 && (!Line.FacilityRow.DataTable ||
			Line.FacilityRow.DataTable->GetRowStruct() != FCSFacilityDataRow::StaticStruct()))
			return Fail(ECSCombinedOrderFailure::FacilityInvalid,
				NSLOCTEXT("Order", "FacilityTable", "시설 데이터 테이블을 확인해 주세요."));
	}
	Result.Products = EvaluateOrderWithDelivery(Products, DeliveryType);
	Result.Facilities = EvaluateFacilityOrderWithDelivery(Facilities, DeliveryType);
	const bool HasProducts = Result.Products.FailureReason != ECOrderFailureReason::EmptyOrder;
	const bool HasFacilities = Result.Facilities.FailureReason != ECFacilityOrderFailureReason::EmptyOrder;
	if (!HasProducts && !HasFacilities) return Fail(ECSCombinedOrderFailure::EmptyCart,
		NSLOCTEXT("Order", "Empty", "발주할 항목을 선택해 주세요."));
	if (HasProducts && !Result.Products.bSuccess && Result.Products.FailureReason != ECOrderFailureReason::InsufficientFunds)
		return Fail(ECSCombinedOrderFailure::ProductInvalid,
			NSLOCTEXT("Order", "ProductUnavailableOrInvalid", "상품의 발주 가능 여부, 수량 또는 가격 정보를 확인해 주세요."));
	if (HasFacilities && !Result.Facilities.bSuccess && Result.Facilities.FailureReason != ECFacilityOrderFailureReason::InsufficientFunds)
		return Fail(ECSCombinedOrderFailure::FacilityInvalid,
			NSLOCTEXT("Order", "FacilityDataInvalid", "시설의 수량 또는 가격 정보를 확인해 주세요."));
	const int64 Total = int64(Result.Products.TotalCost) + Result.Facilities.TotalCost;
	if (Total > MAX_int32) return Fail(ECSCombinedOrderFailure::CostOverflow,
		NSLOCTEXT("Order", "Overflow", "발주 가능한 금액을 초과했습니다."));
	Result.TotalCost = int32(Total);
	Result.RemainingFunds = Result.CurrentFunds - Result.TotalCost;
	if (!State->CanAfford(Result.TotalCost)) return Fail(ECSCombinedOrderFailure::InsufficientFunds,
		NSLOCTEXT("Order", "Funds", "보유 자금이 부족합니다."));
	Result.bSuccess = true;
	return Result;
}

FCSCombinedOrderResult UCSOrderSubsystem::PlaceCombinedOrder(
	const TArray<FCSOrderLine>& Products, const TArray<FCSFacilityOrderLine>& Facilities,
	ECSOrderDeliveryType DeliveryType)
{
	bool bOrderSucceeded = false;
	ON_SCOPE_EXIT
	{
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this))
			Audio->PlaySFX(bOrderSucceeded ? ECSAudioEvent::OrderSuccess : ECSAudioEvent::OrderFail);
	};
	FCSCombinedOrderResult Result = EvaluateCombinedOrder(Products, Facilities, DeliveryType);
	auto Fail = [&Result](ECSCombinedOrderFailure Reason, const FText& Message)
	{
		Result.bSuccess = false;
		Result.Products.bSuccess = false;
		Result.Facilities.bSuccess = false;
		Result.Failure = Reason;
		Result.Message = Message;
		return Result;
	};
	if (bCombinedCheckoutInProgress) return Fail(ECSCombinedOrderFailure::Busy,
		NSLOCTEXT("Order", "Busy", "발주 처리 중입니다."));
	if (!Result.bSuccess) return Result;
	TGuardValue<bool> Guard(bCombinedCheckoutInProgress, true);
	const bool HasProducts = Result.Products.TotalCrateCount > 0;
	const bool HasFacilities = Result.Facilities.TotalFacilityCount > 0;
	ACSDeliveryPortalActor* ProductPortal = RegisteredDeliveryPortal.Get();
	ACSFacilityDeliveryPortalActor* FacilityPortal = RegisteredFacilityDeliveryPortal.Get();
	if ((HasProducts && (!IsValid(ProductPortal) || !ProductPortal->IsDeliveryConfigured())) ||
		(HasFacilities && (!IsValid(FacilityPortal) || !FacilityPortal->IsDeliveryConfigured())))
		return Fail(ECSCombinedOrderFailure::DeliveryUnavailable,
			NSLOCTEXT("Order", "Delivery", "배송 준비가 되어 있지 않습니다."));
	if (HasFacilities)
	{
		const auto Reason = FacilityPortal->ValidateOrderFacilities(Facilities);
		if (Reason != ECFacilityOrderFailureReason::None)
		{
			Result.Facilities.FailureReason = Reason;
			return Fail(ECSCombinedOrderFailure::DeliveryUnavailable,
				NSLOCTEXT("Order", "FacilityClass", "시설 배송 설정을 확인해 주세요."));
		}
	}
	ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	TrySubscribeToStoreGameState();
	TArray<AActor*> SpawnedActors;
	if (DeliveryType == ECSOrderDeliveryType::Immediate)
	{
		EnsureDeliverySpawnIndicesForBusinessDay(State->CurrentBusinessDay);
		if (Result.Products.TotalCrateCount > MAX_int32 - NextProductDeliverySpawnIndex ||
			Result.Facilities.TotalFacilityCount > MAX_int32 - NextFacilityDeliverySpawnIndex)
			return Fail(ECSCombinedOrderFailure::CostOverflow,
				NSLOCTEXT("Order", "SpawnOverflow", "배송 가능한 수량을 초과했습니다."));
		bool SpawnOK = true;
		if (HasProducts)
		{
			TArray<ACSDeliveryCrateActor*> Crates;
			SpawnOK = ProductPortal->SpawnOrderCrates(Products, Crates, NextProductDeliverySpawnIndex)
				&& Crates.Num() == Result.Products.TotalCrateCount;
			for (ACSDeliveryCrateActor* Crate : Crates) SpawnedActors.Add(Crate);
		}
		if (SpawnOK && HasFacilities)
		{
			TArray<AActor*> Actors;
			Result.Facilities.FailureReason = FacilityPortal->SpawnOrderFacilities(Facilities, Actors, NextFacilityDeliverySpawnIndex);
			SpawnOK = Result.Facilities.FailureReason == ECFacilityOrderFailureReason::None
				&& Actors.Num() == Result.Facilities.TotalFacilityCount;
			SpawnedActors.Append(Actors);
		}
		if (!SpawnOK)
		{
			DestroySpawnedActors(SpawnedActors);
			return Fail(ECSCombinedOrderFailure::SpawnFailed,
				NSLOCTEXT("Order", "Spawn", "배송에 실패하여 결제하지 않았습니다. 다시 시도해 주세요."));
		}
	}
	if (!State->TrySpendFunds(Result.TotalCost))
	{
		DestroySpawnedActors(SpawnedActors);
		return Fail(ECSCombinedOrderFailure::InsufficientFunds,
			NSLOCTEXT("Order", "Funds", "보유 자금이 부족합니다."));
	}
	if (HasFacilities)
	{
		RecordShelfPurchases(Facilities);
	}
	if (DeliveryType == ECSOrderDeliveryType::Immediate)
	{
		NextProductDeliverySpawnIndex += Result.Products.TotalCrateCount;
		NextFacilityDeliverySpawnIndex += Result.Facilities.TotalFacilityCount;
	}
	else
	{
		const int32 Day = State->CurrentBusinessDay < MAX_int32 ? State->CurrentBusinessDay + 1 : MAX_int32;
		if (HasProducts)
		{
			auto& Pending = PendingProductOrders.AddDefaulted_GetRef();
			Pending.OrderLines = Products;
			Pending.DeliveryBusinessDay = Day;
		}
		if (HasFacilities)
		{
			auto& Pending = PendingFacilityOrders.AddDefaulted_GetRef();
			Pending.OrderLines = Facilities;
			Pending.DeliveryBusinessDay = Day;
		}
	}
	Result.RemainingFunds = State->GetCurrentFunds();
	Result.Message = NSLOCTEXT("Order", "Complete", "발주가 완료되었습니다.");
	bOrderSucceeded = true;
	if (HasProducts)
	{
		Result.Products.bSuccess = true;
		Result.Products.FailureReason = ECOrderFailureReason::None;
		OnOrderPlaced.Broadcast(Result.Products);
	}
	if (HasFacilities)
	{
		Result.Facilities.bSuccess = true;
		Result.Facilities.FailureReason = ECFacilityOrderFailureReason::None;
		OnFacilityOrderPlaced.Broadcast(Result.Facilities);
	}
	if (DeliveryType == ECSOrderDeliveryType::Immediate)
	{
		if (HasProducts && IsValid(ProductPortal)) ProductPortal->OnOrderDelivered(Result.Products.TotalCrateCount);
		if (HasFacilities && IsValid(FacilityPortal)) FacilityPortal->OnFacilityOrderDelivered(Result.Facilities.TotalFacilityCount);
	}
	return Result;
}

FCSOrderResult UCSOrderSubsystem::EvaluateOrder(
	const TArray<FCSOrderLine>& OrderLines) const
{
	return EvaluateOrderWithDelivery(OrderLines, ECSOrderDeliveryType::NextDay);
}

FCSOrderResult UCSOrderSubsystem::EvaluateOrderWithDelivery(
	const TArray<FCSOrderLine>& OrderLines,
	ECSOrderDeliveryType DeliveryType) const
{
	FCSOrderResult Result;
	// 기존 발주 계산은 유지하고 정산 분류에 필요한 배송 방식만 결과에 보존합니다.
	Result.bImmediateDelivery = DeliveryType == ECSOrderDeliveryType::Immediate;

	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (StoreGameState == nullptr)
	{
		Result.FailureReason = ECOrderFailureReason::StoreGameStateUnavailable;
		return Result;
	}

	int64 TotalCost = 0;
	int64 TotalCrateCount = 0;
	int64 TotalItemQuantity = 0;
	bool bHasSelectedProduct = false;

	for (const FCSOrderLine& OrderLine : OrderLines)
	{
		if (OrderLine.CrateCount == 0)
		{
			continue;
		}

		if (OrderLine.CrateCount < 0)
		{
			Result.FailureReason = ECOrderFailureReason::InvalidCrateCount;
			return Result;
		}

		if (OrderLine.ProductRow.DataTable == nullptr ||
			OrderLine.ProductRow.RowName.IsNone())
		{
			Result.FailureReason = ECOrderFailureReason::InvalidProductRow;
			return Result;
		}

		const FCSProductDataRow* ProductData =
			OrderLine.ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSOrderSubsystem::EvaluateOrder"));
		if (ProductData == nullptr)
		{
			Result.FailureReason = ECOrderFailureReason::InvalidProductRow;
			return Result;
		}

		if (!ProductData->bOrderable)
		{
			Result.FailureReason = ECOrderFailureReason::ProductNotOrderable;
			return Result;
		}

		if (ProductData->PurchasePrice < 0)
		{
			Result.FailureReason = ECOrderFailureReason::InvalidPurchasePrice;
			return Result;
		}

		if (ProductData->OrderPackQuantity < 1)
		{
			Result.FailureReason = ECOrderFailureReason::InvalidPackQuantity;
			return Result;
		}

		const int64 LineItemQuantity =
			static_cast<int64>(ProductData->OrderPackQuantity) * OrderLine.CrateCount;
		if (LineItemQuantity > MAX_int32)
		{
			Result.FailureReason = ECOrderFailureReason::CostOverflow;
			return Result;
		}

		const int64 BaseLineCost =
			static_cast<int64>(ProductData->PurchasePrice) * LineItemQuantity;
		int64 FinalLineCost = 0;
		if (!TryCalculateDeliveryAdjustedCost(
				BaseLineCost,
				DeliveryType,
				FinalLineCost) ||
			TotalCost > MAX_int32 - FinalLineCost ||
			TotalCrateCount > MAX_int32 - OrderLine.CrateCount ||
			TotalItemQuantity > MAX_int32 - LineItemQuantity)
		{
			Result.FailureReason = ECOrderFailureReason::CostOverflow;
			return Result;
		}

		TotalCost += FinalLineCost;
		TotalCrateCount += OrderLine.CrateCount;
		TotalItemQuantity += LineItemQuantity;
		bHasSelectedProduct = true;
	}

	if (!bHasSelectedProduct)
	{
		Result.FailureReason = ECOrderFailureReason::EmptyOrder;
		return Result;
	}

	Result.TotalCost = static_cast<int32>(TotalCost);
	Result.TotalCrateCount = static_cast<int32>(TotalCrateCount);
	Result.TotalItemQuantity = static_cast<int32>(TotalItemQuantity);

	if (!StoreGameState->CanAfford(Result.TotalCost))
	{
		Result.FailureReason = ECOrderFailureReason::InsufficientFunds;
		return Result;
	}

	Result.bSuccess = true;
	Result.FailureReason = ECOrderFailureReason::None;
	return Result;
}

FCSOrderResult UCSOrderSubsystem::PlaceOrder(
	const TArray<FCSOrderLine>& OrderLines)
{
	return PlaceOrderWithDelivery(OrderLines, ECSOrderDeliveryType::NextDay);
}

FCSOrderResult UCSOrderSubsystem::PlaceOrderWithDelivery(
	const TArray<FCSOrderLine>& OrderLines,
	ECSOrderDeliveryType DeliveryType)
{
	bool bOrderSucceeded = false;
	ON_SCOPE_EXIT
	{
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this))
			Audio->PlaySFX(bOrderSucceeded ? ECSAudioEvent::OrderSuccess : ECSAudioEvent::OrderFail);
	};
	if (bCombinedCheckoutInProgress)
	{
		FCSOrderResult Busy;
		Busy.FailureReason = ECOrderFailureReason::DeliveryUnavailable;
		return Busy;
	}
	TGuardValue<bool> CheckoutGuard(bCombinedCheckoutInProgress, true);
	FCSOrderResult Result = EvaluateOrderWithDelivery(OrderLines, DeliveryType);
	if (!Result.bSuccess)
	{
		return Result;
	}

	ACSDeliveryPortalActor* DeliveryPortal = RegisteredDeliveryPortal.Get();
	if (!IsValid(DeliveryPortal) || !DeliveryPortal->IsDeliveryConfigured())
	{
		Result.bSuccess = false;
		Result.FailureReason = ECOrderFailureReason::DeliveryUnavailable;
		return Result;
	}

	UWorld* World = GetWorld();
	ACSStoreGameState* StoreGameState =
		World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (StoreGameState == nullptr)
	{
		Result.bSuccess = false;
		Result.FailureReason = ECOrderFailureReason::StoreGameStateUnavailable;
		return Result;
	}
	TrySubscribeToStoreGameState();

	if (DeliveryType == ECSOrderDeliveryType::Immediate)
	{
		EnsureDeliverySpawnIndicesForBusinessDay(
			StoreGameState->CurrentBusinessDay);
		if (Result.TotalCrateCount >
			MAX_int32 - NextProductDeliverySpawnIndex)
		{
			Result.bSuccess = false;
			Result.FailureReason = ECOrderFailureReason::CostOverflow;
			return Result;
		}

		TArray<ACSDeliveryCrateActor*> SpawnedCrates;
		if (!DeliveryPortal->SpawnOrderCrates(
				OrderLines,
				SpawnedCrates,
				NextProductDeliverySpawnIndex) ||
			SpawnedCrates.Num() != Result.TotalCrateCount)
		{
			TArray<AActor*> SpawnedActors;
			SpawnedActors.Reserve(SpawnedCrates.Num());
			for (ACSDeliveryCrateActor* SpawnedCrate : SpawnedCrates)
			{
				SpawnedActors.Add(SpawnedCrate);
			}
			DestroySpawnedActors(SpawnedActors);
			Result.bSuccess = false;
			Result.FailureReason = ECOrderFailureReason::CrateSpawnFailed;
			return Result;
		}

		if (!StoreGameState->TrySpendFunds(Result.TotalCost))
		{
			TArray<AActor*> SpawnedActors;
			SpawnedActors.Reserve(SpawnedCrates.Num());
			for (ACSDeliveryCrateActor* SpawnedCrate : SpawnedCrates)
			{
				SpawnedActors.Add(SpawnedCrate);
			}
			DestroySpawnedActors(SpawnedActors);
			Result.bSuccess = false;
			Result.FailureReason = ECOrderFailureReason::InsufficientFunds;
			return Result;
		}

		const int32 SpawnedCrateCount = SpawnedCrates.Num();
		NextProductDeliverySpawnIndex += SpawnedCrateCount;
		DeliveryPortal->OnOrderDelivered(SpawnedCrateCount);
		bOrderSucceeded = true;
		OnOrderPlaced.Broadcast(Result);
		return Result;
	}

	if (!StoreGameState->TrySpendFunds(Result.TotalCost))
	{
		Result.bSuccess = false;
		Result.FailureReason = ECOrderFailureReason::InsufficientFunds;
		return Result;
	}

	FCSPendingProductOrder& PendingOrder =
		PendingProductOrders.AddDefaulted_GetRef();
	PendingOrder.OrderLines = OrderLines;
	PendingOrder.DeliveryBusinessDay =
		StoreGameState->CurrentBusinessDay < MAX_int32
			? StoreGameState->CurrentBusinessDay + 1
			: MAX_int32;

	Result.bSuccess = true;
	Result.FailureReason = ECOrderFailureReason::None;

	// 실제 배송과 분리하여 자금 차감 및 Pending 접수가 끝난 시점에 방송합니다.
	bOrderSucceeded = true;
	OnOrderPlaced.Broadcast(Result);

	return Result;
}

FCSFacilityOrderResult UCSOrderSubsystem::EvaluateFacilityOrder(
	const TArray<FCSFacilityOrderLine>& OrderLines) const
{
	return EvaluateFacilityOrderWithDelivery(
		OrderLines,
		ECSOrderDeliveryType::NextDay);
}

FCSFacilityOrderResult UCSOrderSubsystem::EvaluateFacilityOrderWithDelivery(
	const TArray<FCSFacilityOrderLine>& OrderLines,
	ECSOrderDeliveryType DeliveryType) const
{
	FCSFacilityOrderResult Result;
	// 기존 시설 발주 계산은 유지하고 정산 분류에 필요한 배송 방식만 결과에 보존합니다.
	Result.bImmediateDelivery = DeliveryType == ECSOrderDeliveryType::Immediate;

	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (StoreGameState == nullptr)
	{
		Result.FailureReason =
			ECFacilityOrderFailureReason::StoreGameStateUnavailable;
		return Result;
	}

	int64 TotalCost = 0;
	int64 TotalFacilityCount = 0;
	bool bHasSelectedFacility = false;

	for (const FCSFacilityOrderLine& OrderLine : OrderLines)
	{
		if (OrderLine.Quantity == 0)
		{
			continue;
		}

		if (OrderLine.Quantity < 0)
		{
			Result.FailureReason = ECFacilityOrderFailureReason::InvalidQuantity;
			return Result;
		}

		if (OrderLine.FacilityRow.DataTable == nullptr ||
			OrderLine.FacilityRow.RowName.IsNone())
		{
			Result.FailureReason = ECFacilityOrderFailureReason::InvalidFacilityRow;
			return Result;
		}

		const FCSFacilityDataRow* FacilityData =
			OrderLine.FacilityRow.GetRow<FCSFacilityDataRow>(
				TEXT("UCSOrderSubsystem::EvaluateFacilityOrder"));
		if (FacilityData == nullptr)
		{
			Result.FailureReason = ECFacilityOrderFailureReason::InvalidFacilityRow;
			return Result;
		}

		if (FacilityData->PurchasePriceGold < 0)
		{
			Result.FailureReason = ECFacilityOrderFailureReason::InvalidPurchasePrice;
			return Result;
		}

		const int64 BaseLineCost =
			CalculateFacilityPurchaseCost(OrderLine.FacilityRow, OrderLine.Quantity);
		int64 FinalLineCost = 0;
		if (!TryCalculateDeliveryAdjustedCost(
				BaseLineCost,
				DeliveryType,
				FinalLineCost) ||
			TotalCost > MAX_int32 - FinalLineCost ||
			TotalFacilityCount > MAX_int32 - OrderLine.Quantity)
		{
			Result.FailureReason = ECFacilityOrderFailureReason::CostOverflow;
			return Result;
		}

		TotalCost += FinalLineCost;
		TotalFacilityCount += OrderLine.Quantity;
		bHasSelectedFacility = true;
	}

	if (!bHasSelectedFacility)
	{
		Result.FailureReason = ECFacilityOrderFailureReason::EmptyOrder;
		return Result;
	}

	Result.TotalCost = static_cast<int32>(TotalCost);
	Result.TotalFacilityCount = static_cast<int32>(TotalFacilityCount);
	if (!StoreGameState->CanAfford(Result.TotalCost))
	{
		Result.FailureReason = ECFacilityOrderFailureReason::InsufficientFunds;
		return Result;
	}

	Result.bSuccess = true;
	Result.FailureReason = ECFacilityOrderFailureReason::None;
	return Result;
}

FCSFacilityOrderResult UCSOrderSubsystem::PlaceFacilityOrder(
	const TArray<FCSFacilityOrderLine>& OrderLines)
{
	return PlaceFacilityOrderWithDelivery(
		OrderLines,
		ECSOrderDeliveryType::NextDay);
}

FCSFacilityOrderResult UCSOrderSubsystem::PlaceFacilityOrderWithDelivery(
	const TArray<FCSFacilityOrderLine>& OrderLines,
	ECSOrderDeliveryType DeliveryType)
{
	bool bOrderSucceeded = false;
	ON_SCOPE_EXIT
	{
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this))
			Audio->PlaySFX(bOrderSucceeded ? ECSAudioEvent::OrderSuccess : ECSAudioEvent::OrderFail);
	};
	if (bCombinedCheckoutInProgress)
	{
		FCSFacilityOrderResult Busy;
		Busy.FailureReason = ECFacilityOrderFailureReason::DeliveryUnavailable;
		return Busy;
	}
	TGuardValue<bool> CheckoutGuard(bCombinedCheckoutInProgress, true);
	FCSFacilityOrderResult Result = EvaluateFacilityOrderWithDelivery(
		OrderLines,
		DeliveryType);
	if (!Result.bSuccess)
	{
		return Result;
	}

	ACSFacilityDeliveryPortalActor* DeliveryPortal =
		RegisteredFacilityDeliveryPortal.Get();
	if (!IsValid(DeliveryPortal) || !DeliveryPortal->IsDeliveryConfigured())
	{
		Result.bSuccess = false;
		Result.FailureReason = ECFacilityOrderFailureReason::DeliveryUnavailable;
		return Result;
	}

	const ECFacilityOrderFailureReason DeliveryValidationFailure =
		DeliveryPortal->ValidateOrderFacilities(OrderLines);
	if (DeliveryValidationFailure != ECFacilityOrderFailureReason::None)
	{
		Result.bSuccess = false;
		Result.FailureReason = DeliveryValidationFailure;
		return Result;
	}

	UWorld* World = GetWorld();
	ACSStoreGameState* StoreGameState =
		World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (StoreGameState == nullptr)
	{
		Result.bSuccess = false;
		Result.FailureReason =
			ECFacilityOrderFailureReason::StoreGameStateUnavailable;
		return Result;
	}
	TrySubscribeToStoreGameState();

	if (DeliveryType == ECSOrderDeliveryType::Immediate)
	{
		EnsureDeliverySpawnIndicesForBusinessDay(
			StoreGameState->CurrentBusinessDay);
		if (Result.TotalFacilityCount >
			MAX_int32 - NextFacilityDeliverySpawnIndex)
		{
			Result.bSuccess = false;
			Result.FailureReason = ECFacilityOrderFailureReason::CostOverflow;
			return Result;
		}

		TArray<AActor*> SpawnedFacilities;
		const ECFacilityOrderFailureReason SpawnFailureReason =
			DeliveryPortal->SpawnOrderFacilities(
				OrderLines,
				SpawnedFacilities,
				NextFacilityDeliverySpawnIndex);
		if (SpawnFailureReason != ECFacilityOrderFailureReason::None ||
			SpawnedFacilities.Num() != Result.TotalFacilityCount)
		{
			DestroySpawnedActors(SpawnedFacilities);
			Result.bSuccess = false;
			Result.FailureReason =
				SpawnFailureReason != ECFacilityOrderFailureReason::None
					? SpawnFailureReason
					: ECFacilityOrderFailureReason::FacilitySpawnFailed;
			return Result;
		}

		if (!StoreGameState->TrySpendFunds(Result.TotalCost))
		{
			DestroySpawnedActors(SpawnedFacilities);
			Result.bSuccess = false;
			Result.FailureReason =
				ECFacilityOrderFailureReason::InsufficientFunds;
			return Result;
		}

		RecordShelfPurchases(OrderLines);
		const int32 SpawnedFacilityCount = SpawnedFacilities.Num();
		NextFacilityDeliverySpawnIndex += SpawnedFacilityCount;
		DeliveryPortal->OnFacilityOrderDelivered(SpawnedFacilityCount);
		bOrderSucceeded = true;
		OnFacilityOrderPlaced.Broadcast(Result);
		return Result;
	}

	if (!StoreGameState->TrySpendFunds(Result.TotalCost))
	{
		Result.bSuccess = false;
		Result.FailureReason = ECFacilityOrderFailureReason::InsufficientFunds;
		return Result;
	}

	RecordShelfPurchases(OrderLines);
	FCSPendingFacilityOrder& PendingOrder =
		PendingFacilityOrders.AddDefaulted_GetRef();
	PendingOrder.OrderLines = OrderLines;
	PendingOrder.DeliveryBusinessDay =
		StoreGameState->CurrentBusinessDay < MAX_int32
			? StoreGameState->CurrentBusinessDay + 1
			: MAX_int32;

	Result.bSuccess = true;
	Result.FailureReason = ECFacilityOrderFailureReason::None;
	bOrderSucceeded = true;
	OnFacilityOrderPlaced.Broadcast(Result);
	return Result;
}

int32 UCSOrderSubsystem::CalculateProductOrderLineCost(
	const FDataTableRowHandle& ProductRow,
	int32 CrateCount,
	ECSOrderDeliveryType DeliveryType) const
{
	if (CrateCount <= 0 || ProductRow.DataTable == nullptr ||
		ProductRow.RowName.IsNone())
	{
		return 0;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(
			TEXT("UCSOrderSubsystem::CalculateProductOrderLineCost"));
	if (ProductData == nullptr || !ProductData->bOrderable || ProductData->PurchasePrice < 0 ||
		ProductData->OrderPackQuantity < 1)
	{
		return 0;
	}

	const int64 ItemQuantity =
		static_cast<int64>(ProductData->OrderPackQuantity) * CrateCount;
	if (ItemQuantity > MAX_int32)
	{
		return 0;
	}

	int64 FinalLineCost = 0;
	const int64 BaseLineCost =
		static_cast<int64>(ProductData->PurchasePrice) * ItemQuantity;
	return TryCalculateDeliveryAdjustedCost(
		BaseLineCost,
		DeliveryType,
		FinalLineCost)
		? static_cast<int32>(FinalLineCost)
		: 0;
}

int32 UCSOrderSubsystem::CalculateFacilityOrderLineCost(
	const FDataTableRowHandle& FacilityRow,
	int32 Quantity,
	ECSOrderDeliveryType DeliveryType) const
{
	if (Quantity <= 0 || FacilityRow.DataTable == nullptr ||
		FacilityRow.RowName.IsNone())
	{
		return 0;
	}

	const FCSFacilityDataRow* FacilityData =
		FacilityRow.GetRow<FCSFacilityDataRow>(
			TEXT("UCSOrderSubsystem::CalculateFacilityOrderLineCost"));
	if (FacilityData == nullptr || FacilityData->PurchasePriceGold < 0)
	{
		return 0;
	}

	int64 FinalLineCost = 0;
	const int64 BaseLineCost = CalculateFacilityPurchaseCost(FacilityRow, Quantity);
	return TryCalculateDeliveryAdjustedCost(
		BaseLineCost,
		DeliveryType,
		FinalLineCost)
		? static_cast<int32>(FinalLineCost)
		: 0;
}

int32 UCSOrderSubsystem::GetNextFacilityUnitPrice(const FDataTableRowHandle& FacilityRow) const
{
	return static_cast<int32>(FMath::Min<int64>(CalculateFacilityPurchaseCost(FacilityRow, 1), MAX_int32));
}

int64 UCSOrderSubsystem::CalculateFacilityPurchaseCost(
	const FDataTableRowHandle& FacilityRow,
	int32 Quantity) const
{
	const FCSFacilityDataRow* FacilityData = FacilityRow.GetRow<FCSFacilityDataRow>(
		TEXT("UCSOrderSubsystem::CalculateFacilityPurchaseCost"));
	if (FacilityData == nullptr || Quantity <= 0)
	{
		return 0;
	}

	const int64 BasePrice = FacilityData->PurchasePriceGold;
	if (FacilityData->FacilityType != ECSFacilityType::Shelf)
	{
		return BasePrice * Quantity;
	}

	const int32 BoughtCount = PurchasedShelfCounts.FindRef(FacilityRow.RowName);
	int64 TotalCost = 0;
	for (int32 Offset = 0; Offset < Quantity; ++Offset)
	{
		const float Ratio = FMath::Min(
			ShelfFirstPriceRatio + ShelfPriceStepRatio * static_cast<float>(BoughtCount + Offset),
			ShelfMaxPriceRatio);
		TotalCost += FMath::RoundToInt64(BasePrice * Ratio / ShelfPriceRounding) * ShelfPriceRounding;
	}
	return TotalCost;
}

void UCSOrderSubsystem::RecordShelfPurchases(const TArray<FCSFacilityOrderLine>& OrderLines)
{
	for (const FCSFacilityOrderLine& Line : OrderLines)
	{
		const FCSFacilityDataRow* FacilityData = Line.FacilityRow.GetRow<FCSFacilityDataRow>(
			TEXT("UCSOrderSubsystem::RecordShelfPurchases"));
		if (FacilityData != nullptr && FacilityData->FacilityType == ECSFacilityType::Shelf && Line.Quantity > 0)
		{
			PurchasedShelfCounts.FindOrAdd(Line.FacilityRow.RowName) += Line.Quantity;
		}
	}
}

int32 UCSOrderSubsystem::GetImmediateDeliveryFeePercent() const
{
	return ImmediateDeliveryFeePercent;
}

bool UCSOrderSubsystem::RegisterDeliveryPortal(ACSDeliveryPortalActor* Portal)
{
	if (!IsValid(Portal))
	{
		return false;
	}

	if (RegisteredDeliveryPortal.Get() == Portal)
	{
		TrySubscribeToStoreGameState();
		return true;
	}

	if (RegisteredDeliveryPortal.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[OrderSubsystem] 이미 다른 배송 포탈이 등록되어 있습니다."));
		return false;
	}

	RegisteredDeliveryPortal = Portal;
	TrySubscribeToStoreGameState();
	return true;
}

void UCSOrderSubsystem::UnregisterDeliveryPortal(ACSDeliveryPortalActor* Portal)
{
	if (RegisteredDeliveryPortal.Get() == Portal)
	{
		RegisteredDeliveryPortal.Reset();
	}
}

bool UCSOrderSubsystem::RegisterFacilityDeliveryPortal(
	ACSFacilityDeliveryPortalActor* Portal)
{
	if (!IsValid(Portal))
	{
		return false;
	}

	if (RegisteredFacilityDeliveryPortal.Get() == Portal)
	{
		TrySubscribeToStoreGameState();
		return true;
	}

	if (RegisteredFacilityDeliveryPortal.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[OrderSubsystem] 이미 다른 시설 배송 포탈이 등록되어 있습니다."));
		return false;
	}

	RegisteredFacilityDeliveryPortal = Portal;
	TrySubscribeToStoreGameState();
	return true;
}

void UCSOrderSubsystem::UnregisterFacilityDeliveryPortal(
	ACSFacilityDeliveryPortalActor* Portal)
{
	if (RegisteredFacilityDeliveryPortal.Get() == Portal)
	{
		RegisteredFacilityDeliveryPortal.Reset();
	}
}

void UCSOrderSubsystem::TrySubscribeToStoreGameState()
{
	UWorld* World = GetWorld();
	ACSStoreGameState* StoreGameState =
		World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(StoreGameState))
	{
		return;
	}

	if (SubscribedStoreGameState != StoreGameState)
	{
		UnsubscribeFromStoreGameState();
		SubscribedStoreGameState = StoreGameState;
	}

	StoreGameState->OnBusinessDayStarted.AddUniqueDynamic(
		this,
		&UCSOrderSubsystem::HandleBusinessDayStarted);
}

void UCSOrderSubsystem::UnsubscribeFromStoreGameState()
{
	if (IsValid(SubscribedStoreGameState))
	{
		SubscribedStoreGameState->OnBusinessDayStarted.RemoveDynamic(
			this,
			&UCSOrderSubsystem::HandleBusinessDayStarted);
	}

	SubscribedStoreGameState = nullptr;
}

void UCSOrderSubsystem::EnsureDeliverySpawnIndicesForBusinessDay(
	int32 BusinessDay)
{
	if (DeliverySpawnIndexBusinessDay == BusinessDay)
	{
		return;
	}

	DeliverySpawnIndexBusinessDay = BusinessDay;
	NextProductDeliverySpawnIndex = 0;
	NextFacilityDeliverySpawnIndex = 0;
}

void UCSOrderSubsystem::HandleBusinessDayStarted(int32 NewBusinessDay)
{
	// 접수 순서대로 처리하며 성공한 실제 생성 수량만 다음 주문의 위치 offset에 반영합니다.
	EnsureDeliverySpawnIndicesForBusinessDay(NewBusinessDay);
	for (int32 PendingIndex = 0;
		PendingIndex < PendingProductOrders.Num();)
	{
		const FCSPendingProductOrder& PendingOrder =
			PendingProductOrders[PendingIndex];
		if (PendingOrder.DeliveryBusinessDay > NewBusinessDay)
		{
			++PendingIndex;
			continue;
		}

		ACSDeliveryPortalActor* DeliveryPortal = RegisteredDeliveryPortal.Get();
		if (!IsValid(DeliveryPortal) || !DeliveryPortal->IsDeliveryConfigured())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[OrderSubsystem] 상품 Pending 배송 실패: 배송 포탈을 사용할 수 없습니다. 예정일=%d"),
				PendingOrder.DeliveryBusinessDay);
			++PendingIndex;
			continue;
		}

		int64 ExpectedCrateCount = 0;
		for (const FCSOrderLine& OrderLine : PendingOrder.OrderLines)
		{
			ExpectedCrateCount += OrderLine.CrateCount;
		}
		if (ExpectedCrateCount < 0 || ExpectedCrateCount > MAX_int32 ||
			ExpectedCrateCount > MAX_int32 - NextProductDeliverySpawnIndex)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[OrderSubsystem] 상품 Pending 배송 수량 또는 SpawnIndex가 범위를 초과했습니다. 예정일=%d"),
				PendingOrder.DeliveryBusinessDay);
			++PendingIndex;
			continue;
		}

		TArray<ACSDeliveryCrateActor*> SpawnedCrates;
		if (!DeliveryPortal->SpawnOrderCrates(
			PendingOrder.OrderLines,
			SpawnedCrates,
			NextProductDeliverySpawnIndex) ||
			SpawnedCrates.Num() != static_cast<int32>(ExpectedCrateCount))
		{
			TArray<AActor*> SpawnedActors;
			SpawnedActors.Reserve(SpawnedCrates.Num());
			for (ACSDeliveryCrateActor* SpawnedCrate : SpawnedCrates)
			{
				SpawnedActors.Add(SpawnedCrate);
			}
			DestroySpawnedActors(SpawnedActors);
			UE_LOG(LogTemp, Warning,
				TEXT("[OrderSubsystem] 상품 Pending 배송 Spawn에 실패했습니다. 예정일=%d"),
				PendingOrder.DeliveryBusinessDay);
			++PendingIndex;
			continue;
		}

		const int32 SpawnedCrateCount = SpawnedCrates.Num();
		NextProductDeliverySpawnIndex += SpawnedCrateCount;
		PendingProductOrders.RemoveAt(PendingIndex);
		DeliveryPortal->OnOrderDelivered(SpawnedCrateCount);
	}

	for (int32 PendingIndex = 0;
		PendingIndex < PendingFacilityOrders.Num();)
	{
		const FCSPendingFacilityOrder& PendingOrder =
			PendingFacilityOrders[PendingIndex];
		if (PendingOrder.DeliveryBusinessDay > NewBusinessDay)
		{
			++PendingIndex;
			continue;
		}

		ACSFacilityDeliveryPortalActor* DeliveryPortal =
			RegisteredFacilityDeliveryPortal.Get();
		if (!IsValid(DeliveryPortal) || !DeliveryPortal->IsDeliveryConfigured())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[OrderSubsystem] 시설 Pending 배송 실패: 배송 포탈을 사용할 수 없습니다. 예정일=%d"),
				PendingOrder.DeliveryBusinessDay);
			++PendingIndex;
			continue;
		}

		int64 ExpectedFacilityCount = 0;
		for (const FCSFacilityOrderLine& OrderLine : PendingOrder.OrderLines)
		{
			ExpectedFacilityCount += OrderLine.Quantity;
		}
		if (ExpectedFacilityCount < 0 || ExpectedFacilityCount > MAX_int32 ||
			ExpectedFacilityCount > MAX_int32 - NextFacilityDeliverySpawnIndex)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[OrderSubsystem] 시설 Pending 배송 수량 또는 SpawnIndex가 범위를 초과했습니다. 예정일=%d"),
				PendingOrder.DeliveryBusinessDay);
			++PendingIndex;
			continue;
		}

		TArray<AActor*> SpawnedFacilities;
		const ECFacilityOrderFailureReason SpawnFailureReason =
			DeliveryPortal->SpawnOrderFacilities(
				PendingOrder.OrderLines,
				SpawnedFacilities,
				NextFacilityDeliverySpawnIndex);
		if (SpawnFailureReason != ECFacilityOrderFailureReason::None)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[OrderSubsystem] 시설 Pending 배송 Spawn에 실패했습니다. 예정일=%d, 실패코드=%d"),
				PendingOrder.DeliveryBusinessDay,
				static_cast<int32>(SpawnFailureReason));
			++PendingIndex;
			continue;
		}

		if (SpawnedFacilities.Num() != static_cast<int32>(ExpectedFacilityCount))
		{
			DestroySpawnedActors(SpawnedFacilities);

			UE_LOG(LogTemp, Warning,
				TEXT("[OrderSubsystem] 시설 Pending 배송 수량이 일치하지 않습니다. 예정=%d, 생성=%d"),
				static_cast<int32>(ExpectedFacilityCount),
				SpawnedFacilities.Num());
			++PendingIndex;
			continue;
		}

		const int32 SpawnedFacilityCount = SpawnedFacilities.Num();
		NextFacilityDeliverySpawnIndex += SpawnedFacilityCount;
		PendingFacilityOrders.RemoveAt(PendingIndex);
		DeliveryPortal->OnFacilityOrderDelivered(SpawnedFacilityCount);
	}
}
