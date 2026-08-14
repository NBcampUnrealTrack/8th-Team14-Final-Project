#pragma once

#include "CoreMinimal.h"
#include "Data/CSOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSOrderSubsystem.generated.h"

class ACSDeliveryPortalActor;

// 발주가 실제로 성공(자금 차감 + 배송 상자 생성)했을 때만 방송됨
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnOrderPlaced,
	const FCSOrderResult&,
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

	bool RegisterDeliveryPortal(ACSDeliveryPortalActor* Portal);
	void UnregisterDeliveryPortal(ACSDeliveryPortalActor* Portal);

	// 발주가 성공적으로 완료될 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Store|Order")
	FCSOnOrderPlaced OnOrderPlaced;

private:
	TWeakObjectPtr<ACSDeliveryPortalActor> RegisteredDeliveryPortal;
};
