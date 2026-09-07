#pragma once

#include "CoreMinimal.h"
#include "Data/CSOrderTypes.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryPortalActor.generated.h"

class USceneComponent;
class UArrowComponent;
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
	bool SpawnOrderCrates(
		const TArray<FCSOrderLine>& OrderLines,
		TArray<ACSDeliveryCrateActor*>& OutSpawnedCrates,
		int32 StartingSpawnIndex);

	// 배송 상자 더미와 겹치지 않는 직원 전용 접근 지점을 NavMesh 위 위치로 반환합니다.
	bool TryGetEmployeeDeliveryApproachPoint(FVector& OutLocation) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnOrderDelivered(int32 SpawnedCrateCount);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DeliverySpawnOrigin;

	// 직원은 상자 액터 자체가 아니라 이 화살표 위치까지 이동한 뒤 가까운 배송 상자를 집습니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> DeliveryApproachPoint;

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
