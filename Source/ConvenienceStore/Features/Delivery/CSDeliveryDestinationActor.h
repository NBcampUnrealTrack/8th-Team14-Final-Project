#pragma once

#include "CoreMinimal.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryDestinationActor.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class ACSDeliveryOrderIndicatorActor;
class UCSDeliverySettlementWidget;
class ACSStorePlayerController;
class UCSDeliveryOrderSubsystem;

// 수레가 도착했을 때 주문 상품을 검사하고 판매를 완료하는 배달 지점입니다.
UCLASS()
class CONVENIENCESTORE_API ACSDeliveryDestinationActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSDeliveryDestinationActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	// 서브시스템이 무작위 주문의 목적지로 사용할 수 있는지 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool CanReceiveRandomDeliveryOrder() const { return bAllowRandomDeliveryOrder && AssignedOrderId == INDEX_NONE; }

	// 주문 UI와 상호작용 안내에 표시할 목적지의 친화적인 이름을 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	FText GetDestinationDisplayName() const { return DestinationDisplayName; }

	// 주문 상태에 따라 목적지 외곽선을 켜거나 끕니다.
	UFUNCTION(BlueprintCallable, Category = "Delivery Order|Highlight")
	void SetDeliveryOrderHighlight(bool bShouldHighlight, int32 StencilValue = 3);

	// 기한 초과처럼 플레이어 상호작용 없이 발생한 결과를 전용 정산 UI로 표시합니다.
	void ShowDeliverySettlementForPlayer(
		ACSStorePlayerController* StorePlayerController,
		const FText& Message,
		ECSDeliverySettlementResult Result,
		int32 RewardGold = 0,
		int32 ReputationReward = 0);

	// 현재 목적지에 할당된 주문을 기록합니다.
	void AssignDeliveryOrder(int32 OrderId);
	void ClearDeliveryOrder(int32 OrderId);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 수레 전체가 도착했는지 확인하는 배달 완료 범위입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> DeliveryZone;

	// DeliveryZone 위에 표시할 단일 배달 구역 가이드 메시입니다.
	// 블루프린트에서 DeliveryZoneCube 같은 메시를 지정해 사용합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DeliveryZoneGuide;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Delivery Order")
	bool bAllowRandomDeliveryOrder = true;

	// 에디터에서 배달 지점마다 지정하는 표시용 이름입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Delivery Order")
	FText DestinationDisplayName;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery Order")
	int32 AssignedOrderId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery Order")
	bool bIsHighlightedForDelivery = false;

	// 배달 완료·실패 사유와 보상을 표시할 전용 정산 위젯 클래스입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order|UI")
	TSubclassOf<UCSDeliverySettlementWidget> DeliverySettlementWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliverySettlementWidget> ActiveDeliverySettlementWidget;

	// 자신에게 배정된 수락 주문을 가리킬 때만 목적지 위에 표시할 화살표입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order|Indicator")
	TSubclassOf<ACSDeliveryOrderIndicatorActor> DeliveryOrderIndicatorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order|Indicator")
	FVector DeliveryOrderIndicatorOffset = FVector(0.0f, 0.0f, 300.0f);

	UPROPERTY(Transient)
	TObjectPtr<ACSDeliveryOrderIndicatorActor> DeliveryOrderIndicator;

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderSubsystem> BoundDeliveryOrderSubsystem;

	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Order")
	void OnDeliveryCompleted();

	// 상호작용을 수행한 플레이어에게 배달 처리 결과 정산 창을 엽니다.
	void ShowDeliverySettlement(
		AActor* Interactor,
		const FText& Message,
		ECSDeliverySettlementResult Result,
		int32 RewardGold,
		int32 ReputationReward);

	UFUNCTION()
	void HandleDeliveryOrdersChanged();

	void RefreshDeliveryOrderIndicator();

	void SetDeliveryZoneGuideVisible(bool bShouldShow);
};
