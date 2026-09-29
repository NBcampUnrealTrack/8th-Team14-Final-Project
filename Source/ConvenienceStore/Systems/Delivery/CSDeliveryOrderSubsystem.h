#pragma once

#include "CoreMinimal.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSDeliveryOrderSubsystem.generated.h"

class UDataTable;
class UCSDeliveryOrderSettingsDataAsset;
class ACSProductPickupActor;
class UCSSaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnDeliveryOrderChanged,
	const FCSDeliveryOrderRuntime&,
	Order);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnDeliveryOrdersChanged);

// 영업 시간 중 배달 주문을 만들고, 재고 예약·기한·완료 처리를 일관되게 관리합니다.
UCLASS()
class CONVENIENCESTORE_API UCSDeliveryOrderSubsystem : public UTickableWorldSubsystem,
	public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	// ICSSaveParticipant
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	FCSDeliveryOrderRuntime GetCurrentOrder() const;

	// 현재 수락 대기 또는 진행 중인 주문 전체를 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	TArray<FCSDeliveryOrderRuntime> GetDeliveryOrders() const { return DeliveryOrders; }

	/** 완료·실패·기한 초과 결과를 최근 순서로 보관합니다. 게시판 기록용입니다. */
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	TArray<FCSDeliveryOrderRuntime> GetRecentDeliveryResults() const { return RecentDeliveryResults; }

	// 오늘 생성된 전체 주문 수와 현재 수락된 주문 수를 UI 요약에 사용합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	int32 GetTodayGeneratedOrderCount() const { return GeneratedOrderCountToday; }

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	int32 GetDailyOrderLimit() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	int32 GetAcceptedOrderCount() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool GetDeliveryOrderById(int32 OrderId, FCSDeliveryOrderRuntime& OutOrder) const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool HasActiveOrder() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool CanAcceptCurrentOrder(FText& OutReason) const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool CanAcceptOrder(int32 OrderId, FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool AcceptCurrentOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool AcceptOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool DeclineCurrentOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool DeclineOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool CancelCurrentOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool CancelOrder(int32 OrderId, FText& OutMessage);

	/** 마법진 액터가 레벨 설정 에셋을 등록합니다. 단말기 없이 주문을 생성하는 진입점입니다. */
	void ConfigureOrderSettings(UCSDeliveryOrderSettingsDataAsset* InSettings);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order|Dispatch")
	bool SetActiveDispatchOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintPure, Category = "Delivery Order|Dispatch")
	int32 GetActiveDispatchOrderId() const { return ActiveDispatchOrderId; }

	UFUNCTION(BlueprintPure, Category = "Delivery Order|Dispatch")
	FCSDeliveryOrderRuntime GetActiveDispatchOrder() const;

	/** 마법진 안에 실제로 놓인 상품만 검사해 선택된 주문을 발송합니다. */
	bool TryCompleteDispatchOrder(
		const TArray<ACSProductPickupActor*>& AvailableProducts,
		FText& OutMessage,
		int32& OutRewardGold,
		int32& OutReputationReward);

	// 준비 표시와 실제 발송이 동일한 상품·수량 검사를 사용합니다. 상품을 소비하지 않습니다.
	bool FindDispatchProducts(const TArray<ACSProductPickupActor*>& AvailableProducts,
		TArray<ACSProductPickupActor*>& OutProducts, FText& OutMessage) const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order|Stock")
	int32 GetReservedQuantity(FName ProductId) const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order|Stock")
	int32 GetAvailableQuantityForOrder(const FDataTableRowHandle& ProductRow) const;

	UFUNCTION(BlueprintCallable, Category = "Delivery Order|Debug")
	bool GenerateOrderNow(FText& OutMessage);

	UPROPERTY(BlueprintAssignable, Category = "Delivery Order")
	FCSOnDeliveryOrderChanged OnDeliveryOrderChanged;

	UPROPERTY(BlueprintAssignable, Category = "Delivery Order")
	FCSOnDeliveryOrdersChanged OnDeliveryOrdersChanged;

private:
	bool TryGenerateRandomOrder(FText& OutMessage);
	bool IsStoreOperating() const;
	void RefreshBusinessDayState();
	void ScheduleNextOrder(float CurrentGameMinutes);
	int32 GetMaximumLineCount() const;
	int32 FindOrderIndex(int32 OrderId) const;
	FCSDeliveryOrderRuntime* FindOrderMutable(int32 OrderId);
	const FCSDeliveryOrderRuntime* FindOrder(int32 OrderId) const;
	int32 FindPrimaryOrderIndex() const;
	int32 FindDispatchOrderIndex() const;
	void ReserveOrderStock(const FCSDeliveryOrderRuntime& Order);
	void ReleaseOrderReservation(const FCSDeliveryOrderRuntime& Order);
	void BroadcastOrdersChanged(const FCSDeliveryOrderRuntime& ChangedOrder);
	void RemoveOrderAt(int32 OrderIndex, ECSDeliveryOrderStatus FinalStatus);
	void ShowAcceptanceExpiredToast(const FCSDeliveryOrderRuntime& ExpiredOrder);
	void ShowDeadlineExpiredToast(const FCSDeliveryOrderRuntime& ExpiredOrder);
	void ShowStoreClosedFailureToast(const FCSDeliveryOrderRuntime& FailedOrder);
	void ShowDeliveryToast(const FText& Title, const FText& Message) const;

	// 단일 CurrentOrder 대신 여러 수락 대기·진행 중 주문을 함께 보관합니다.
	UPROPERTY(Transient)
	TArray<FCSDeliveryOrderRuntime> DeliveryOrders;

	UPROPERTY(Transient)
	TArray<FCSDeliveryOrderRuntime> RecentDeliveryResults;

	int32 MaximumRecentDeliveryResultCount = 5;

	// 기존 단일 주문 UI와의 호환을 위해 가장 우선순위가 높은 주문을 반환합니다.
	UPROPERTY(Transient)
	FCSDeliveryOrderRuntime CurrentOrder;

	UPROPERTY(Transient)
	TMap<FName, int32> ReservedQuantities;

	UPROPERTY(Transient)
	int32 LastObservedBusinessDay = INDEX_NONE;

	UPROPERTY(Transient)
	int32 GeneratedOrderCountToday = 0;

	UPROPERTY(Transient)
	float NextOrderGenerationMinutes = -1.0f;

	// 강화로 오늘의 주문 한도가 바뀌면 이미 예약한 다음 주문도 남은 시간에 맞춰 다시 배치합니다.
	UPROPERTY(Transient)
	int32 LastScheduledDailyOrderLimit = INDEX_NONE;

	UPROPERTY(Transient)
	int32 NextOrderId = 1;

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderSettingsDataAsset> OrderSettings;

	UPROPERTY(Transient)
	int32 ActiveDispatchOrderId = INDEX_NONE;

	float AcceptanceWindowMinutes = 120.0f;
	float DeliveryWindowMinutes = 180.0f;
	int32 MinimumProductQuantity = 1;
	int32 MaximumProductQuantity = 3;
	float InStockProductPreference = 0.7f;
};
