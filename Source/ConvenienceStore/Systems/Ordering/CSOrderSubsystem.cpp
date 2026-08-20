#include "Systems/Ordering/CSOrderSubsystem.h"

#include "Data/CSFacilityTypes.h"
#include "Data/CSProductTypes.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Delivery/CSDeliveryPortalActor.h"
#include "Features/Delivery/CSFacilityDeliveryPortalActor.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

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
	RegisteredDeliveryPortal.Reset();
	RegisteredFacilityDeliveryPortal.Reset();
	DeliverySpawnIndexBusinessDay = INDEX_NONE;
	NextProductDeliverySpawnIndex = 0;
	NextFacilityDeliverySpawnIndex = 0;

	Super::Deinitialize();
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

		const int64 BaseLineCost =
			static_cast<int64>(FacilityData->PurchasePriceGold) *
			OrderLine.Quantity;
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

		const int32 SpawnedFacilityCount = SpawnedFacilities.Num();
		NextFacilityDeliverySpawnIndex += SpawnedFacilityCount;
		DeliveryPortal->OnFacilityOrderDelivered(SpawnedFacilityCount);
		OnFacilityOrderPlaced.Broadcast(Result);
		return Result;
	}

	if (!StoreGameState->TrySpendFunds(Result.TotalCost))
	{
		Result.bSuccess = false;
		Result.FailureReason = ECFacilityOrderFailureReason::InsufficientFunds;
		return Result;
	}

	FCSPendingFacilityOrder& PendingOrder =
		PendingFacilityOrders.AddDefaulted_GetRef();
	PendingOrder.OrderLines = OrderLines;
	PendingOrder.DeliveryBusinessDay =
		StoreGameState->CurrentBusinessDay < MAX_int32
			? StoreGameState->CurrentBusinessDay + 1
			: MAX_int32;

	Result.bSuccess = true;
	Result.FailureReason = ECFacilityOrderFailureReason::None;
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
	if (ProductData == nullptr || ProductData->PurchasePrice < 0 ||
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
	const int64 BaseLineCost =
		static_cast<int64>(FacilityData->PurchasePriceGold) * Quantity;
	return TryCalculateDeliveryAdjustedCost(
		BaseLineCost,
		DeliveryType,
		FinalLineCost)
		? static_cast<int32>(FinalLineCost)
		: 0;
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
