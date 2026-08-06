#pragma once

#include "CoreMinimal.h"
#include "Data/CSOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSOrderSubsystem.generated.h"

class ACSDeliveryPortalActor;

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

private:
	TWeakObjectPtr<ACSDeliveryPortalActor> RegisteredDeliveryPortal;
};
