#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityOrderTypes.h"
#include "Data/CSOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSOrderSubsystem.generated.h"

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
class CONVENIENCESTORE_API UCSOrderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

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
	void TrySubscribeToStoreGameState();
	void UnsubscribeFromStoreGameState();
	void EnsureDeliverySpawnIndicesForBusinessDay(int32 BusinessDay);

	UFUNCTION()
	void HandleBusinessDayStarted(int32 NewBusinessDay);

	TWeakObjectPtr<ACSDeliveryPortalActor> RegisteredDeliveryPortal;
	TWeakObjectPtr<ACSFacilityDeliveryPortalActor> RegisteredFacilityDeliveryPortal;

	// UPROPERTY로 보관하여 Pending RowHandle의 DataTable 참조가 GC에 안전하게 유지됩니다.
	UPROPERTY()
	TArray<FCSPendingProductOrder> PendingProductOrders;

	UPROPERTY()
	TArray<FCSPendingFacilityOrder> PendingFacilityOrders;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> SubscribedStoreGameState;

	int32 DeliverySpawnIndexBusinessDay = INDEX_NONE;
	int32 NextProductDeliverySpawnIndex = 0;
	int32 NextFacilityDeliverySpawnIndex = 0;
};
