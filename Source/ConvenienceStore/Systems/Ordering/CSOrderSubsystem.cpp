#include "Systems/Ordering/CSOrderSubsystem.h"

#include "Data/CSFacilityTypes.h"
#include "Data/CSProductTypes.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Delivery/CSDeliveryPortalActor.h"
#include "Features/Delivery/CSFacilityDeliveryPortalActor.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

namespace
{
bool TryParseRequiredStoreLevel(FName StoreLevelCondition, int32& OutRequiredLevel)
{
	OutRequiredLevel = 1;
	if (StoreLevelCondition.IsNone())
	{
		return true;
	}

	const FString ConditionString = StoreLevelCondition.ToString();
	const FString Prefix = TEXT("StoreLv_");
	if (!ConditionString.StartsWith(Prefix, ESearchCase::CaseSensitive))
	{
		return false;
	}

	const FString LevelString = ConditionString.RightChop(Prefix.Len());
	if (LevelString.IsEmpty())
	{
		return false;
	}

	int64 ParsedLevel = 0;
	for (const TCHAR Character : LevelString)
	{
		if (!FChar::IsDigit(Character))
		{
			return false;
		}

		const int32 Digit = Character - TEXT('0');
		if (ParsedLevel > (MAX_int32 - Digit) / 10)
		{
			return false;
		}
		ParsedLevel = ParsedLevel * 10 + Digit;
	}

	if (ParsedLevel < 1)
	{
		return false;
	}

	OutRequiredLevel = static_cast<int32>(ParsedLevel);
	return true;
}
}

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

	// 자금 차감과 배송 상자 생성까지 전부 끝난 시점에만 방송
	OnOrderPlaced.Broadcast(Result);

	return Result;
}

FCSFacilityOrderResult UCSOrderSubsystem::EvaluateFacilityOrder(
	const TArray<FCSFacilityOrderLine>& OrderLines) const
{
	FCSFacilityOrderResult Result;

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

		int32 RequiredStoreLevel = 1;
		if (!TryParseRequiredStoreLevel(
			FacilityData->StoreLevelUnlockCondition,
			RequiredStoreLevel))
		{
			Result.FailureReason =
				ECFacilityOrderFailureReason::InvalidStoreLevelCondition;
			return Result;
		}

		if (!StoreGameState->IsStoreLevelRequirementMet(RequiredStoreLevel))
		{
			Result.FailureReason = ECFacilityOrderFailureReason::StoreLevelLocked;
			return Result;
		}

		if (!StoreGameState->IsReputationRequirementMet(
			FacilityData->ReputationUnlockCondition))
		{
			Result.FailureReason = ECFacilityOrderFailureReason::ReputationLocked;
			return Result;
		}

		if (!FacilityData->QuestUnlockCondition.IsNone())
		{
			Result.FailureReason =
				ECFacilityOrderFailureReason::QuestRequirementUnsupported;
			return Result;
		}

		const int64 LineCost =
			static_cast<int64>(FacilityData->PurchasePriceGold) *
			OrderLine.Quantity;
		if (LineCost > MAX_int32 ||
			TotalCost > MAX_int32 - LineCost ||
			TotalFacilityCount > MAX_int32 - OrderLine.Quantity)
		{
			Result.FailureReason = ECFacilityOrderFailureReason::CostOverflow;
			return Result;
		}

		TotalCost += LineCost;
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
	FCSFacilityOrderResult Result = EvaluateFacilityOrder(OrderLines);
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

	TArray<AActor*> SpawnedFacilities;
	const ECFacilityOrderFailureReason SpawnFailureReason =
		DeliveryPortal->SpawnOrderFacilities(OrderLines, SpawnedFacilities);
	auto DestroySpawnedFacilities = [&SpawnedFacilities]()
	{
		for (AActor* SpawnedFacility : SpawnedFacilities)
		{
			if (IsValid(SpawnedFacility))
			{
				SpawnedFacility->Destroy();
			}
		}
		SpawnedFacilities.Reset();
	};

	if (SpawnFailureReason != ECFacilityOrderFailureReason::None)
	{
		DestroySpawnedFacilities();
		Result.bSuccess = false;
		Result.FailureReason = SpawnFailureReason;
		return Result;
	}

	if (SpawnedFacilities.Num() != Result.TotalFacilityCount)
	{
		DestroySpawnedFacilities();
		Result.bSuccess = false;
		Result.FailureReason = ECFacilityOrderFailureReason::SpawnedFacilityInvalid;
		return Result;
	}

	UWorld* World = GetWorld();
	ACSStoreGameState* StoreGameState =
		World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (StoreGameState == nullptr)
	{
		DestroySpawnedFacilities();
		Result.bSuccess = false;
		Result.FailureReason =
			ECFacilityOrderFailureReason::StoreGameStateUnavailable;
		return Result;
	}

	if (!StoreGameState->TrySpendFunds(Result.TotalCost))
	{
		DestroySpawnedFacilities();
		Result.bSuccess = false;
		Result.FailureReason = ECFacilityOrderFailureReason::InsufficientFunds;
		return Result;
	}

	Result.bSuccess = true;
	Result.FailureReason = ECFacilityOrderFailureReason::None;
	DeliveryPortal->OnFacilityOrderDelivered(SpawnedFacilities.Num());
	OnFacilityOrderPlaced.Broadcast(Result);
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

bool UCSOrderSubsystem::RegisterFacilityDeliveryPortal(
	ACSFacilityDeliveryPortalActor* Portal)
{
	if (!IsValid(Portal))
	{
		return false;
	}

	if (RegisteredFacilityDeliveryPortal.Get() == Portal)
	{
		return true;
	}

	if (RegisteredFacilityDeliveryPortal.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[OrderSubsystem] 이미 다른 시설 배송 포탈이 등록되어 있습니다."));
		return false;
	}

	RegisteredFacilityDeliveryPortal = Portal;
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
