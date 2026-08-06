#pragma once

#include "CoreMinimal.h"
#include "Data/CSOrderTypes.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryPortalActor.generated.h"

class USceneComponent;
class ACSDeliveryCrateActor;

UCLASS()
class CONVENIENCESTORE_API ACSDeliveryPortalActor : public AActor
{
	GENERATED_BODY()

public:
	ACSDeliveryPortalActor();

	bool IsDeliveryConfigured() const;
	bool SpawnOrderCrates(
		const TArray<FCSOrderLine>& OrderLines,
		TArray<ACSDeliveryCrateActor*>& OutSpawnedCrates);

	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnOrderDelivered(int32 SpawnedCrateCount);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DeliverySpawnOrigin;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery")
	TSubclassOf<ACSDeliveryCrateActor> DeliveryCrateClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 CratesPerRow = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery",
		meta = (ClampMin = "0"))
	float ForwardSpacing = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery",
		meta = (ClampMin = "0"))
	float RightSpacing = 120.0f;
};
