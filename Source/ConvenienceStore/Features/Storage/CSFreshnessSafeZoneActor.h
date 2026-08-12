#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "GameFramework/Actor.h"
#include "CSFreshnessSafeZoneActor.generated.h"

class UBoxComponent;
class USceneComponent;
class UPrimitiveComponent;

UCLASS()
class CONVENIENCESTORE_API ACSFreshnessSafeZoneActor : public AActor
{
	GENERATED_BODY()

public:
	ACSFreshnessSafeZoneActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SafeZoneBox;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Freshness")
	ECSProductShelfType SafeStorageType = ECSProductShelfType::ChilledShelf;

	UFUNCTION()
	void HandleSafeZoneBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSafeZoneEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

private:
	bool IsSupportedSafeStorageType() const;
	void RegisterOverlappingActor(AActor* OtherActor);
	void UnregisterOverlappingActor(AActor* OtherActor);

	TSet<TWeakObjectPtr<AActor>> RegisteredActors;
};
