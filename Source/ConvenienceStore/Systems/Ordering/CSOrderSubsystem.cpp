#include "Systems/Ordering/CSOrderSubsystem.h"

#include "Data/CSProductTypes.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Delivery/CSDeliveryPortalActor.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

FCSOrderResult UCSOrderSubsystem::EvaluateOrder(
	const TArray<FCSOrderLine>& OrderLines) const
{
	FCSOrderResult Result;

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

		const int64 LineCost =
			static_cast<int64>(ProductData->PurchasePrice) * LineItemQuantity;
		if (LineCost > MAX_int32 ||
			TotalCost > MAX_int32 - LineCost ||
			TotalCrateCount > MAX_int32 - OrderLine.CrateCount ||
			TotalItemQuantity > MAX_int32 - LineItemQuantity)
		{
			Result.FailureReason = ECOrderFailureReason::CostOverflow;
			return Result;
		}

		TotalCost += LineCost;
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
	FCSOrderResult Result = EvaluateOrder(OrderLines);
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

	TArray<ACSDeliveryCrateActor*> SpawnedCrates;
	if (!DeliveryPortal->SpawnOrderCrates(OrderLines, SpawnedCrates))
	{
		Result.bSuccess = false;
		Result.FailureReason = ECOrderFailureReason::CrateSpawnFailed;
		return Result;
	}

	auto DestroySpawnedCrates = [&SpawnedCrates]()
	{
		for (ACSDeliveryCrateActor* SpawnedCrate : SpawnedCrates)
		{
			if (IsValid(SpawnedCrate))
			{
				SpawnedCrate->Destroy();
			}
		}
		SpawnedCrates.Reset();
	};

	UWorld* World = GetWorld();
	ACSStoreGameState* StoreGameState =
		World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (StoreGameState == nullptr)
	{
		DestroySpawnedCrates();
		Result.bSuccess = false;
		Result.FailureReason = ECOrderFailureReason::StoreGameStateUnavailable;
		return Result;
	}

	if (!StoreGameState->TrySpendFunds(Result.TotalCost))
	{
		DestroySpawnedCrates();
		Result.bSuccess = false;
		Result.FailureReason = ECOrderFailureReason::InsufficientFunds;
		return Result;
	}

	Result.bSuccess = true;
	Result.FailureReason = ECOrderFailureReason::None;
	DeliveryPortal->OnOrderDelivered(SpawnedCrates.Num());
	return Result;
}

bool UCSOrderSubsystem::RegisterDeliveryPortal(ACSDeliveryPortalActor* Portal)
{
	if (!IsValid(Portal))
	{
		return false;
	}

	if (RegisteredDeliveryPortal.Get() == Portal)
	{
		return true;
	}

	if (RegisteredDeliveryPortal.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[OrderSubsystem] 이미 다른 배송 포탈이 등록되어 있습니다."));
		return false;
	}

	RegisteredDeliveryPortal = Portal;
	return true;
}

void UCSOrderSubsystem::UnregisterDeliveryPortal(ACSDeliveryPortalActor* Portal)
{
	if (RegisteredDeliveryPortal.Get() == Portal)
	{
		RegisteredDeliveryPortal.Reset();
	}
}
