#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityOrderTypes.h"
#include "Data/CSOrderTypes.h"
#include "Data/CSCombinedOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSOrderSubsystem.generated.h"

class UCSSaveGame;
class ACSDeliveryPortalActor;
class ACSFacilityDeliveryPortalActor;
class ACSStoreGameState;

// 발주 접수(자금 차감 + Pending 등록)가 성공했을 때만 방송됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnOrderPlaced,
	const FCSOrderResult&,
	Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnFacilityOrderPlaced,
	const FCSFacilityOrderResult&,
	Result);

// 현재 월드 세션에서 다음 영업일까지 보관할 상품 발주입니다.
USTRUCT()
struct FCSPendingProductOrder
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FCSOrderLine> OrderLines;

	UPROPERTY()
	int32 DeliveryBusinessDay = 0;
};

// 현재 월드 세션에서 다음 영업일까지 보관할 시설 발주입니다.
USTRUCT()
struct FCSPendingFacilityOrder
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FCSFacilityOrderLine> OrderLines;

	UPROPERTY()
	int32 DeliveryBusinessDay = 0;
};

UCLASS()
class CONVENIENCESTORE_API UCSOrderSubsystem : public UWorldSubsystem,
	public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// ICSSaveParticipant
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	UFUNCTION(BlueprintPure, Category="Store|Order|Combined")
	FCSCombinedOrderResult EvaluateCombinedOrder(const TArray<FCSOrderLine>& Products,
		const TArray<FCSFacilityOrderLine>& Facilities, ECSOrderDeliveryType DeliveryType) const;

	UFUNCTION(BlueprintCallable, Category="Store|Order|Combined")
	FCSCombinedOrderResult PlaceCombinedOrder(const TArray<FCSOrderLine>& Products,
		const TArray<FCSFacilityOrderLine>& Facilities, ECSOrderDeliveryType DeliveryType);

	// 호환용 이름: 진행도와 무관하게 유효한 시설 행이면 true입니다.
	UFUNCTION(BlueprintPure, Category="Store|Order|Unlock")
	bool IsFacilityUnlocked(const FDataTableRowHandle& Row) const;

	// 호환용 이름: 유효한 상품 행의 bOrderable만 검사합니다.
	UFUNCTION(BlueprintPure, Category="Store|Order|Unlock")
	bool IsProductUnlocked(const FDataTableRowHandle& Row) const;

	UFUNCTION(BlueprintPure, Category="Store|Order|Unlock")
	TArray<FName> GetUnlockedFacilityRowNames(UDataTable* Table) const;

	UFUNCTION(BlueprintPure, Category = "Store|Order")
	FCSOrderResult EvaluateOrder(const TArray<FCSOrderLine>& OrderLines) const;

	UFUNCTION(BlueprintPure, Category = "Store|Order")
	FCSOrderResult EvaluateOrderWithDelivery(
		const TArray<FCSOrderLine>& OrderLines,
		ECSOrderDeliveryType DeliveryType) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Order")
	FCSOrderResult PlaceOrder(const TArray<FCSOrderLine>& OrderLines);

	UFUNCTION(BlueprintCallable, Category = "Store|Order")
	FCSOrderResult PlaceOrderWithDelivery(
		const TArray<FCSOrderLine>& OrderLines,
		ECSOrderDeliveryType DeliveryType);

	UFUNCTION(BlueprintPure, Category = "Store|Order|Facility")
	FCSFacilityOrderResult EvaluateFacilityOrder(
		const TArray<FCSFacilityOrderLine>& OrderLines) const;

	UFUNCTION(BlueprintPure, Category = "Store|Order|Facility")
	FCSFacilityOrderResult EvaluateFacilityOrderWithDelivery(
		const TArray<FCSFacilityOrderLine>& OrderLines,
		ECSOrderDeliveryType DeliveryType) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Order|Facility")
	FCSFacilityOrderResult PlaceFacilityOrder(
		const TArray<FCSFacilityOrderLine>& OrderLines);

	UFUNCTION(BlueprintCallable, Category = "Store|Order|Facility")
	FCSFacilityOrderResult PlaceFacilityOrderWithDelivery(
		const TArray<FCSFacilityOrderLine>& OrderLines,
		ECSOrderDeliveryType DeliveryType);

	UFUNCTION(BlueprintPure, Category = "Store|Order")
	int32 CalculateProductOrderLineCost(
		const FDataTableRowHandle& ProductRow,
		int32 CrateCount,
		ECSOrderDeliveryType DeliveryType) const;

	UFUNCTION(BlueprintPure, Category = "Store|Order|Facility")
	int32 CalculateFacilityOrderLineCost(
		const FDataTableRowHandle& FacilityRow,
		int32 Quantity,
		ECSOrderDeliveryType DeliveryType) const;

	UFUNCTION(BlueprintPure, Category = "Store|Order")
	int32 GetImmediateDeliveryFeePercent() const;

	// 발주창 표시용. 이 시설을 지금 하나 더 살 때의 값입니다. 배송비는 빠져 있습니다.
	int32 GetNextFacilityUnitPrice(const FDataTableRowHandle& FacilityRow) const;

	bool RegisterDeliveryPortal(ACSDeliveryPortalActor* Portal);
	void UnregisterDeliveryPortal(ACSDeliveryPortalActor* Portal);
	bool RegisterFacilityDeliveryPortal(ACSFacilityDeliveryPortalActor* Portal);
	void UnregisterFacilityDeliveryPortal(ACSFacilityDeliveryPortalActor* Portal);

	// 자금 차감과 Pending 등록까지 완료되어 발주 접수에 성공할 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Store|Order")
	FCSOnOrderPlaced OnOrderPlaced;

	UPROPERTY(BlueprintAssignable, Category = "Store|Order|Facility")
	FCSOnFacilityOrderPlaced OnFacilityOrderPlaced;

private:
	bool bCombinedCheckoutInProgress = false;
	void TrySubscribeToStoreGameState();
	void UnsubscribeFromStoreGameState();
	void EnsureDeliverySpawnIndicesForBusinessDay(int32 BusinessDay);
	// 진열대는 같은 종류를 살수록 비싸집니다.
	int64 CalculateFacilityPurchaseCost(const FDataTableRowHandle& FacilityRow, int32 Quantity) const;
	void RecordShelfPurchases(const TArray<FCSFacilityOrderLine>& OrderLines);

	UFUNCTION()
	void HandleBusinessDayStarted(int32 NewBusinessDay);

	TWeakObjectPtr<ACSDeliveryPortalActor> RegisteredDeliveryPortal;
	TWeakObjectPtr<ACSFacilityDeliveryPortalActor> RegisteredFacilityDeliveryPortal;

	// UPROPERTY로 보관하여 Pending RowHandle의 DataTable 참조가 GC에 안전하게 유지됩니다.
	UPROPERTY()
	TArray<FCSPendingProductOrder> PendingProductOrders;

	UPROPERTY()
	TArray<FCSPendingFacilityOrder> PendingFacilityOrders;

	// 진열대 종류별로 직접 산 개수. 보유 수로 세면 튜토리얼이 준 진열대를 못 받은 경우 첫 구매가 값을 안 올린다
	UPROPERTY()
	TMap<FName, int32> PurchasedShelfCounts;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> SubscribedStoreGameState;

	int32 DeliverySpawnIndexBusinessDay = INDEX_NONE;
	int32 NextProductDeliverySpawnIndex = 0;
	int32 NextFacilityDeliverySpawnIndex = 0;
};
