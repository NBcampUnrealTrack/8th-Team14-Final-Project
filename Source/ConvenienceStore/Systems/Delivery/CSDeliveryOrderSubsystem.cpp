#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Engine/GameInstance.h"

#include "Data/CSProductTypes.h"
#include "Data/CSDeliveryOrderSettingsDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/SaveLoad/CSSaveRecordUtils.h"
#include "Systems/SaveLoad/CSSaveSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

void UCSDeliveryOrderSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSDeliveryOrderSaveRecord& Record = OutSave.DeliveryOrder;

	Record.DeliveryOrders.Reset(DeliveryOrders.Num());
	for (const FCSDeliveryOrderRuntime& Order : DeliveryOrders)
	{
		Record.DeliveryOrders.Add(CSSaveRecordUtils::ToRecord(Order));
	}

	Record.RecentDeliveryResults.Reset(RecentDeliveryResults.Num());
	for (const FCSDeliveryOrderRuntime& Order : RecentDeliveryResults)
	{
		Record.RecentDeliveryResults.Add(CSSaveRecordUtils::ToRecord(Order));
	}

	Record.ReservedQuantities = ReservedQuantities;
	Record.NextOrderId = NextOrderId;
	Record.GeneratedOrderCountToday = GeneratedOrderCountToday;
	Record.NextOrderGenerationMinutes = NextOrderGenerationMinutes;
	Record.LastScheduledDailyOrderLimit = LastScheduledDailyOrderLimit;
	Record.ActiveDispatchOrderId = ActiveDispatchOrderId;

	// 로드 직후 RefreshBusinessDayState()가 같은 날을 새 영업일로 인식해 주문을 다시 생성하는 상황을 방지
	Record.LastObservedBusinessDay = LastObservedBusinessDay;
}

void UCSDeliveryOrderSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	const FCSDeliveryOrderSaveRecord& Record = InSave.DeliveryOrder;

	DeliveryOrders.Reset(Record.DeliveryOrders.Num());
	for (const FCSDeliveryOrderRecord& OrderRecord : Record.DeliveryOrders)
	{
		DeliveryOrders.Add(CSSaveRecordUtils::ToRuntime(OrderRecord));
	}

	RecentDeliveryResults.Reset(Record.RecentDeliveryResults.Num());
	for (const FCSDeliveryOrderRecord& OrderRecord : Record.RecentDeliveryResults)
	{
		RecentDeliveryResults.Add(CSSaveRecordUtils::ToRuntime(OrderRecord));
	}

	ReservedQuantities = Record.ReservedQuantities;
	NextOrderId = FMath::Max(1, Record.NextOrderId);
	LastObservedBusinessDay = Record.LastObservedBusinessDay;
	GeneratedOrderCountToday = Record.GeneratedOrderCountToday;
	NextOrderGenerationMinutes = Record.NextOrderGenerationMinutes;
	LastScheduledDailyOrderLimit = Record.LastScheduledDailyOrderLimit;
	ActiveDispatchOrderId = Record.ActiveDispatchOrderId;

	// 단일 주문 호환 UI가 읽는 대표 주문을 다시 설정
	const int32 PrimaryIndex = FindPrimaryOrderIndex();
	CurrentOrder = FCSDeliveryOrderRuntime();
	if (DeliveryOrders.IsValidIndex(PrimaryIndex))
	{
		CurrentOrder = DeliveryOrders[PrimaryIndex];
	}

	OnDeliveryOrdersChanged.Broadcast();
}

namespace
{
#if !UE_BUILD_SHIPPING
FAutoConsoleCommandWithWorld CmdDeliveryOrderNow(
	TEXT("cs.DeliveryOrderNow"),
	TEXT("배달 주문 대기 시간을 건너뜁니다. 영업시간, 하루 한도, 주문 한 건 제한은 유지합니다."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		auto* Orders = World ? World->GetSubsystem<UCSDeliveryOrderSubsystem>() : nullptr;
		if (!Orders)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Delivery] 플레이 중인 게임에서 실행하세요."));
			return;
		}
		FText Message;
		const bool bGenerated = Orders->GenerateOrderNow(Message);
		UE_LOG(LogTemp, Display, TEXT("[Delivery] %s"), *Message.ToString());
		// 성공 알림은 기존 주문 생성 경로에서 표시합니다.
		if (!bGenerated)
		{
			if (auto* Controller = Cast<ACSStorePlayerController>(World->GetFirstPlayerController()))
			{
				Controller->ShowGameplayToast(
					NSLOCTEXT("Delivery", "DebugOrderRejected", "테스트 주문 생성 불가"), Message);
			}
		}
	}));
#endif

bool IsOrderInProgress(const ECSDeliveryOrderStatus Status)
{
	return Status == ECSDeliveryOrderStatus::PendingAcceptance || Status == ECSDeliveryOrderStatus::Accepted;
}
}

void UCSDeliveryOrderSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 다음 틱에 예약된 세이브 복원보다 먼저 실행되면 저장된 주문 대신 새 주문을 생성
	// 복원이 끝날 때까지 주문 갱신을 건너뜀
	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* OwningGameInstance = World->GetGameInstance())
		{
			const UCSSaveSubsystem* SaveSubsystem = OwningGameInstance->GetSubsystem<UCSSaveSubsystem>();
			if (SaveSubsystem != nullptr && SaveSubsystem->HasPendingLoad())
			{
				return;
			}
		}
	}

	RefreshBusinessDayState();
	ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState)) return;

	const float CurrentMinutes = StoreGameState->CurrentGameMinutes;
	for (int32 Index = DeliveryOrders.Num() - 1; Index >= 0; --Index)
	{
		const FCSDeliveryOrderRuntime& Order = DeliveryOrders[Index];
		if (Order.Status == ECSDeliveryOrderStatus::PendingAcceptance && CurrentMinutes >= Order.AcceptanceDeadlineMinutes)
		{
			const FCSDeliveryOrderRuntime ExpiredOrder = Order;
			RemoveOrderAt(Index, ECSDeliveryOrderStatus::Expired);
			ShowAcceptanceExpiredToast(ExpiredOrder);
		}
		else if (Order.Status == ECSDeliveryOrderStatus::Accepted &&
			(CurrentMinutes >= Order.DeliveryDeadlineMinutes || !IsStoreOperating()))
		{
			const FCSDeliveryOrderRuntime FailedOrder = Order;
			const bool bDeadlineExpired = CurrentMinutes >= Order.DeliveryDeadlineMinutes;
			StoreGameState->AddReputation(-2, bDeadlineExpired
				? TEXT("배달 주문 기한 초과")
				: TEXT("영업 종료로 배달 주문 실패"));
			RemoveOrderAt(Index, bDeadlineExpired ? ECSDeliveryOrderStatus::Expired : ECSDeliveryOrderStatus::Failed);
			if (bDeadlineExpired)
			{
				ShowDeadlineExpiredToast(FailedOrder);
			}
			else
			{
				ShowStoreClosedFailureToast(FailedOrder);
			}
		}
	}

	const int32 DailyOrderLimit = GetDailyOrderLimit();
	if (!IsStoreOperating() || !DeliveryOrders.IsEmpty() || GeneratedOrderCountToday >= DailyOrderLimit) return;
	// 당일 강화는 이미 도착한 주문을 되돌리지 않으며, 다음 주문만 새 한도에 맞춰 분산합니다.
	if (LastScheduledDailyOrderLimit != DailyOrderLimit)
	{
		if (GeneratedOrderCountToday > 0) NextOrderGenerationMinutes = -1.0f;
		LastScheduledDailyOrderLimit = DailyOrderLimit;
	}
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
	ActiveDispatchOrderId = OrderId;
	BroadcastOrdersChanged(*Order);
	OutMessage = NSLOCTEXT("Delivery", "OrderAccepted", "배달 주문을 수락했습니다.");
	ShowDeliveryToast(
		NSLOCTEXT("Delivery", "OrderAcceptedToastTitle", "배달 주문 수락"),
		FText::Format(
			NSLOCTEXT("Delivery", "OrderAcceptedToast", "{0} 주문을 수락했습니다.\n마법진에 주문 상품을 올려 발송하세요."),
			Order->RecipientDisplayName));
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
	if (ACSStoreGameState* StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>(); IsValid(StoreGameState)) StoreGameState->AddReputation(-1, TEXT("배달 주문 직접 취소"));
	RemoveOrderAt(Index, ECSDeliveryOrderStatus::Canceled);
	OutMessage = NSLOCTEXT("Delivery", "OrderCanceled", "배달 주문을 취소했습니다. 예약 재고가 해제되고 평판이 1 감소했습니다.");
	return true;
}

void UCSDeliveryOrderSubsystem::ConfigureOrderSettings(UCSDeliveryOrderSettingsDataAsset* InSettings)
{
	OrderSettings = InSettings;
}

bool UCSDeliveryOrderSubsystem::SetActiveDispatchOrder(const int32 OrderId, FText& OutMessage)
{
	const FCSDeliveryOrderRuntime* Order = FindOrder(OrderId);
	if (Order == nullptr || Order->Status != ECSDeliveryOrderStatus::Accepted)
	{
		OutMessage = NSLOCTEXT("Delivery", "NoAcceptedDispatchOrder", "발송 준비할 수락된 주문이 없습니다.");
		return false;
	}
	ActiveDispatchOrderId = OrderId;
	OutMessage = FText::Format(
		NSLOCTEXT("Delivery", "DispatchOrderSelected", "{0} 주문을 발송 준비했습니다."),
		Order->RecipientDisplayName);
	ShowDeliveryToast(
		NSLOCTEXT("Delivery", "DispatchReadyToastTitle", "발송 준비 완료"),
		FText::Format(
			NSLOCTEXT("Delivery", "DispatchReadyToast", "{0} 주문을 마법진으로 발송할 수 있습니다."),
			Order->RecipientDisplayName));
	return true;
}

FCSDeliveryOrderRuntime UCSDeliveryOrderSubsystem::GetActiveDispatchOrder() const
{
	const FCSDeliveryOrderRuntime* Order = FindOrder(ActiveDispatchOrderId);
	return Order == nullptr ? FCSDeliveryOrderRuntime() : *Order;
}

bool UCSDeliveryOrderSubsystem::FindDispatchProducts(
	const TArray<ACSProductPickupActor*>& AvailableProducts,
	TArray<ACSProductPickupActor*>& OutProducts, FText& OutMessage) const
{
	OutProducts.Reset();
	OutMessage = FText::GetEmpty();
	const int32 OrderIndex = FindDispatchOrderIndex();
	if (!DeliveryOrders.IsValidIndex(OrderIndex))
	{
		OutMessage = NSLOCTEXT("Delivery", "NoDispatchOrder", "게시판에서 배달 주문을 먼저 수락하세요.");
		return false;
	}

	const FCSDeliveryOrderRuntime& Order = DeliveryOrders[OrderIndex];
	TSet<ACSProductPickupActor*> UsedProducts;
	for (const FCSDeliveryOrderLine& OrderLine : Order.RequiredProducts)
	{
		int32 FoundCount = 0;
		for (ACSProductPickupActor* Product : AvailableProducts)
		{
			if (IsValid(Product) && !UsedProducts.Contains(Product) && Product->GetProductRow() == OrderLine.ProductRow)
			{
				OutProducts.Add(Product);
				UsedProducts.Add(Product);
				if (++FoundCount >= OrderLine.Quantity) break;
			}
		}
		if (FoundCount < OrderLine.Quantity)
		{
			OutProducts.Reset();
			OutMessage = NSLOCTEXT("Delivery", "MissingMagicCircleProducts", "마법진 위에 주문 상품이 부족합니다.");
			return false;
		}
	}
	return true;
}

bool UCSDeliveryOrderSubsystem::TryCompleteDispatchOrder(
	const TArray<ACSProductPickupActor*>& AvailableProducts,
	FText& OutMessage, int32& OutRewardGold, int32& OutReputationReward)
{
	OutRewardGold = 0;
	OutReputationReward = 0;
	TArray<ACSProductPickupActor*> ProductsToDeliver;
	if (!FindDispatchProducts(AvailableProducts, ProductsToDeliver, OutMessage)) return false;
	const int32 OrderIndex = FindDispatchOrderIndex();
	const FCSDeliveryOrderRuntime Order = DeliveryOrders[OrderIndex];
	// Destroy 및 보상 알림에서 재진입하더라도 같은 주문을 다시 발송할 수 없도록 먼저 종료합니다.
	RemoveOrderAt(OrderIndex, ECSDeliveryOrderStatus::Completed);

	for (ACSProductPickupActor* Product : ProductsToDeliver)
	{
		Product->Destroy();
	}
	if (ACSStoreGameState* StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>(); IsValid(StoreGameState))
	{
		StoreGameState->AddFunds(Order.RewardGold);
		StoreGameState->AddReputation(Order.ReputationReward, TEXT("배달 주문 완료 보상"));
	}
	OutRewardGold = Order.RewardGold;
	OutReputationReward = Order.ReputationReward;
	OutMessage = FText::Format(
		NSLOCTEXT("Delivery", "MagicCircleDeliverySuccess", "{0}에 배달을 완료했습니다. +{1} 골드 / 평판 +{2}"),
		Order.RecipientDisplayName,
		FText::AsNumber(OutRewardGold),
		FText::AsNumber(OutReputationReward));
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
	// 자동 생성과 디버그 생성 모두 수락 대기·진행 중 주문을 합쳐 한 건만 허용합니다.
	if (!DeliveryOrders.IsEmpty())
	{
		OutMessage = NSLOCTEXT("Delivery", "ExistingOrder", "기존 배달 주문을 먼저 처리하세요.");
		return false;
	}
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

	if (!IsValid(OrderSettings) || !IsValid(OrderSettings->ProductDataTable))
	{
		OutMessage = NSLOCTEXT("Delivery", "NoOrderSettings", "상품 데이터가 설정된 배달 마법진이 없습니다.");
		return false;
	}
	if (OrderSettings->Recipients.IsEmpty())
	{
		OutMessage = NSLOCTEXT("Delivery", "NoRecipient", "배달 설정에 원격 수신처가 없습니다.");
		return false;
	}

	ACSStoreGameState* StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState))
	{
		OutMessage = NSLOCTEXT("Delivery", "NoStoreGameState", "상점 상태를 찾을 수 없습니다.");
		return false;
	}

	TArray<FName> EligibleRowNames;
	const float ClosingBuffer = FMath::IsFinite(OrderSettings->StopOrdersBeforeClosingMinutes)
		? FMath::Max(1.0f, OrderSettings->StopOrdersBeforeClosingMinutes) : 60.0f;
	if (!IsStoreOperating() || StoreGameState->CurrentGameMinutes >= StoreGameState->AutomaticClosingTimeMinutes - ClosingBuffer)
	{
		OutMessage = NSLOCTEXT("Delivery", "OutsideOrderHours", "현재는 배달 주문이 도착할 수 있는 시간이 아닙니다.");
		return false;
	}
	TArray<FName> InStockRowNames;
	for (const FName RowName : OrderSettings->ProductDataTable->GetRowNames())
	{
		const FCSProductDataRow* ProductData = OrderSettings->ProductDataTable->FindRow<FCSProductDataRow>(RowName, TEXT("UCSDeliveryOrderSubsystem::TryGenerateRandomOrder"));
		if (ProductData == nullptr) continue;
		EligibleRowNames.Add(RowName);
		FDataTableRowHandle ProductRow;
		ProductRow.DataTable = OrderSettings->ProductDataTable;
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
	const FCSDeliveryRecipientInfo& Recipient = OrderSettings->Recipients[FMath::RandRange(0, OrderSettings->Recipients.Num() - 1)];
	NewOrder.RecipientId = Recipient.RecipientId;
	NewOrder.RecipientDisplayName = Recipient.DisplayName;
	NewOrder.RecipientRegionName = Recipient.RegionName;
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
		NewLine.ProductRow.DataTable = OrderSettings->ProductDataTable;
		NewLine.ProductRow.RowName = SelectedRow;
		NewLine.Quantity = FMath::RandRange(MinimumProductQuantity, MaximumProductQuantity);
		if (const FCSProductDataRow* ProductData = NewLine.ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSDeliveryOrderSubsystem::TryGenerateRandomOrder"))) NewOrder.RewardGold += ProductData->RecommendedSalePrice * NewLine.Quantity;
	}
	NewOrder.RewardGold = FMath::Max(100, FMath::RoundToInt(NewOrder.RewardGold * 1.3f));
	NewOrder.ReputationReward = FMath::Clamp(NewOrder.RequiredProducts.Num(), 1, 3);
	NewOrder.AcceptanceDeadlineMinutes = FMath::Min(StoreGameState->AutomaticClosingTimeMinutes, StoreGameState->CurrentGameMinutes + AcceptanceWindowMinutes);
	DeliveryOrders.Add(NewOrder);
	++GeneratedOrderCountToday;
	NextOrderGenerationMinutes = -1.0f;
	BroadcastOrdersChanged(NewOrder);
	OutMessage = NSLOCTEXT("Delivery", "OrderGenerated", "새 배달 주문이 도착했습니다.");
	ShowDeliveryToast(
		NSLOCTEXT("Delivery", "OrderGeneratedToastTitle", "새 배달 주문 도착"),
		FText::Format(
			NSLOCTEXT("Delivery", "OrderGeneratedToast", "{0}에서 주문을 보냈습니다.\n게시판에서 확인하세요."),
			NewOrder.RecipientDisplayName));
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
	LastScheduledDailyOrderLimit = INDEX_NONE;
	for (int32 Index = DeliveryOrders.Num() - 1; Index >= 0; --Index) RemoveOrderAt(Index, ECSDeliveryOrderStatus::Failed);
}

void UCSDeliveryOrderSubsystem::ScheduleNextOrder(const float CurrentGameMinutes)
{
	const UCSDeliveryOrderSettingsDataAsset* Settings = IsValid(OrderSettings) ? OrderSettings.Get() : GetDefault<UCSDeliveryOrderSettingsDataAsset>();
	const float FirstOrderMinimum = FMath::IsFinite(Settings->MinimumOrderIntervalMinutes) ? FMath::Max(1.0f, Settings->MinimumOrderIntervalMinutes) : 45.0f;
	const float FirstOrderMaximum = FMath::IsFinite(Settings->MaximumOrderIntervalMinutes) ? FMath::Max(FirstOrderMinimum, Settings->MaximumOrderIntervalMinutes) : FMath::Max(FirstOrderMinimum, 90.0f);

	// 첫 주문은 개점 후 준비 시간을 보장합니다. 그 뒤에는 하루 전체에 주문을 배치합니다.
	if (GeneratedOrderCountToday == 0)
	{
		NextOrderGenerationMinutes = CurrentGameMinutes + FMath::FRandRange(FirstOrderMinimum, FirstOrderMaximum);
		return;
	}

	const ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(StoreGameState))
	{
		NextOrderGenerationMinutes = -1.0f;
		return;
	}
	const float ClosingBuffer = FMath::IsFinite(Settings->StopOrdersBeforeClosingMinutes)
		? FMath::Max(1.0f, Settings->StopOrdersBeforeClosingMinutes) : 60.0f;
	const float LastOrderTime = StoreGameState->AutomaticClosingTimeMinutes - ClosingBuffer;
	const int32 RemainingOrderCount = FMath::Max(0, GetDailyOrderLimit() - GeneratedOrderCountToday);
	if (RemainingOrderCount <= 0 || CurrentGameMinutes >= LastOrderTime)
	{
		NextOrderGenerationMinutes = -1.0f;
		return;
	}

	// 마지막 주문을 마감 제한 직전에 몰지 않도록 남은 구간 하나를 여유 시간으로 남깁니다.
	const float TargetInterval = (LastOrderTime - CurrentGameMinutes) / static_cast<float>(RemainingOrderCount + 1);
	const float JitterPercent = FMath::IsFinite(Settings->DistributedOrderTimeJitterPercent)
		? FMath::Clamp(Settings->DistributedOrderTimeJitterPercent, 0.0f, 0.45f) : 0.2f;
	const float Jitter = TargetInterval * JitterPercent;
	const float ScheduledInterval = FMath::Max(FirstOrderMinimum, TargetInterval + FMath::FRandRange(-Jitter, Jitter));
	// 마감에 맞춰 간격을 줄이지 않습니다. 시간이 부족하면 오늘 최대 건수를 채우지 않습니다.
	NextOrderGenerationMinutes = CurrentGameMinutes + ScheduledInterval;
}

int32 UCSDeliveryOrderSubsystem::GetDailyOrderLimit() const
{
	const ACSStoreGameState* StoreGameState = GetWorld() == nullptr ? nullptr : GetWorld()->GetGameState<ACSStoreGameState>();
	return IsValid(StoreGameState) ? StoreGameState->GetMagicCircleLevel() : 0;
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

int32 UCSDeliveryOrderSubsystem::FindDispatchOrderIndex() const
{
	const int32 Index = FindOrderIndex(ActiveDispatchOrderId);
	return DeliveryOrders.IsValidIndex(Index) && DeliveryOrders[Index].Status == ECSDeliveryOrderStatus::Accepted
		? Index
		: INDEX_NONE;
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

void UCSDeliveryOrderSubsystem::BroadcastOrdersChanged(const FCSDeliveryOrderRuntime& ChangedOrder)
{
	// 주문 변경 알림 전에 수락·종료 상태를 수첩에 반영하며 월드 인스턴스별로 주문을 구분합니다.
	if (auto* GI = GetWorld()->GetGameInstance())
	{
		const auto* State = GetWorld()->GetGameState<ACSStoreGameState>();
		const FName Scope(*FString::Printf(TEXT("%s:%u"), *GetWorld()->GetMapName(), GetWorld()->GetUniqueID()));
		GI->GetSubsystem<UCSJournalSubsystem>()->RecordDelivery(ChangedOrder, State ? State->CurrentBusinessDay : 1, Scope);
	}
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
	if (PreviousOrder.OrderId == ActiveDispatchOrderId) ActiveDispatchOrderId = INDEX_NONE;
	if (FinalStatus == ECSDeliveryOrderStatus::Completed ||
		FinalStatus == ECSDeliveryOrderStatus::Failed ||
		FinalStatus == ECSDeliveryOrderStatus::Expired)
	{
		RecentDeliveryResults.Insert(FinishedOrder, 0);
		if (RecentDeliveryResults.Num() > MaximumRecentDeliveryResultCount)
		{
			RecentDeliveryResults.SetNum(MaximumRecentDeliveryResultCount);
		}
	}
	DeliveryOrders.RemoveAt(OrderIndex);
	NextOrderGenerationMinutes = -1.0f;
	if (IsStoreOperating())
	{
		ScheduleNextOrder(GetWorld()->GetGameState<ACSStoreGameState>()->CurrentGameMinutes);
	}
	BroadcastOrdersChanged(FinishedOrder);
}

void UCSDeliveryOrderSubsystem::ShowAcceptanceExpiredToast(
	const FCSDeliveryOrderRuntime& ExpiredOrder)
{
	const FText Recipient = ExpiredOrder.RecipientDisplayName.IsEmpty()
		? NSLOCTEXT("Delivery", "UnknownRecipient", "수신처")
		: ExpiredOrder.RecipientDisplayName;
	ShowDeliveryToast(
		NSLOCTEXT("Delivery", "AcceptanceExpiredTitle", "배달 주문 만료"),
		FText::Format(
			NSLOCTEXT("Delivery", "AcceptanceExpiredToast", "{0} 주문을 수락하지 않아 만료되었습니다."),
			Recipient));
}

void UCSDeliveryOrderSubsystem::ShowDeadlineExpiredToast(
	const FCSDeliveryOrderRuntime& ExpiredOrder)
{
	ACSStorePlayerController* StorePlayerController = GetWorld() == nullptr
		? nullptr
		: Cast<ACSStorePlayerController>(GetWorld()->GetFirstPlayerController());
	if (!IsValid(StorePlayerController))
	{
		return;
	}

	const FText Recipient = ExpiredOrder.RecipientDisplayName.IsEmpty()
		? NSLOCTEXT("Delivery", "UnknownRecipient", "수신처")
		: ExpiredOrder.RecipientDisplayName;
	ShowDeliveryToast(
		NSLOCTEXT("Delivery", "DeliveryDeadlineExpiredTitle", "배달 기한 초과"),
		FText::Format(
			NSLOCTEXT("Delivery", "DeliveryDeadlineExpiredToast", "{0} 주문의 배달 기한을 초과했습니다.\n평판이 2 감소했습니다."),
			Recipient));
}

void UCSDeliveryOrderSubsystem::ShowStoreClosedFailureToast(
	const FCSDeliveryOrderRuntime& FailedOrder)
{
	const FText Recipient = FailedOrder.RecipientDisplayName.IsEmpty()
		? NSLOCTEXT("Delivery", "UnknownRecipient", "수신처")
		: FailedOrder.RecipientDisplayName;
	ShowDeliveryToast(
		NSLOCTEXT("Delivery", "StoreClosedFailureTitle", "배달 주문 실패"),
		FText::Format(
			NSLOCTEXT("Delivery", "StoreClosedFailureToast", "영업 종료로 {0} 주문을 발송하지 못했습니다.\n평판이 2 감소했습니다."),
			Recipient));
}

void UCSDeliveryOrderSubsystem::ShowDeliveryToast(const FText& Title, const FText& Message) const
{
	ACSStorePlayerController* StorePlayerController = GetWorld() == nullptr
		? nullptr
		: Cast<ACSStorePlayerController>(GetWorld()->GetFirstPlayerController());
	if (IsValid(StorePlayerController))
	{
		StorePlayerController->ShowGameplayToast(Title, Message);
	}
}
