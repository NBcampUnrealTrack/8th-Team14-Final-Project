#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityOrderTypes.h"
#include "GameFramework/Actor.h"
#include "CSFacilityDeliveryPortalActor.generated.h"

class USceneComponent;

UCLASS()
class CONVENIENCESTORE_API ACSFacilityDeliveryPortalActor : public AActor
{
	GENERATED_BODY()

public:
	ACSFacilityDeliveryPortalActor();

	bool IsDeliveryConfigured() const;
	ECFacilityOrderFailureReason SpawnOrderFacilities(
		const TArray<FCSFacilityOrderLine>& OrderLines,
		TArray<AActor*>& OutSpawnedFacilities);

	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery|Facility")
	void OnFacilityOrderDelivered(int32 SpawnedFacilityCount);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> FacilitySpawnOrigin;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Facility")
	TMap<FName, TSubclassOf<AActor>> FacilityActorClasses;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Facility",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 FacilitiesPerRow = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Facility",
		meta = (ClampMin = "0", UIMin = "0"))
	float ForwardSpacing = 350.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Facility",
		meta = (ClampMin = "0", UIMin = "0"))
	float RightSpacing = 350.0f;
};
