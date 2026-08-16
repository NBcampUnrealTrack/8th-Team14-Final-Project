#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityOrderTypes.h"
#include "Data/CSOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSOrderSubsystem.generated.h"

class ACSDeliveryPortalActor;
class ACSFacilityDeliveryPortalActor;

// 발주가 실제로 성공(자금 차감 + 배송 상자 생성)했을 때만 방송됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnOrderPlaced,
	const FCSOrderResult&,
	Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnFacilityOrderPlaced,
	const FCSFacilityOrderResult&,
	Result);

UCLASS()
class CONVENIENCESTORE_API UCSOrderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Store|Order")
	FCSOrderResult EvaluateOrder(const TArray<FCSOrderLine>& OrderLines) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Order")
	FCSOrderResult PlaceOrder(const TArray<FCSOrderLine>& OrderLines);

	UFUNCTION(BlueprintPure, Category = "Store|Order|Facility")
	FCSFacilityOrderResult EvaluateFacilityOrder(
		const TArray<FCSFacilityOrderLine>& OrderLines) const;

	UFUNCTION(BlueprintCallable, Category = "Store|Order|Facility")
	FCSFacilityOrderResult PlaceFacilityOrder(
		const TArray<FCSFacilityOrderLine>& OrderLines);

	bool RegisterDeliveryPortal(ACSDeliveryPortalActor* Portal);
	void UnregisterDeliveryPortal(ACSDeliveryPortalActor* Portal);
	bool RegisterFacilityDeliveryPortal(ACSFacilityDeliveryPortalActor* Portal);
	void UnregisterFacilityDeliveryPortal(ACSFacilityDeliveryPortalActor* Portal);

	// 발주가 성공적으로 완료될 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Store|Order")
	FCSOnOrderPlaced OnOrderPlaced;

	UPROPERTY(BlueprintAssignable, Category = "Store|Order|Facility")
	FCSOnFacilityOrderPlaced OnFacilityOrderPlaced;

private:
	TWeakObjectPtr<ACSDeliveryPortalActor> RegisteredDeliveryPortal;
	TWeakObjectPtr<ACSFacilityDeliveryPortalActor> RegisteredFacilityDeliveryPortal;
};
