#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSGachaArtifactPickup.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

// 아티팩트 기획 전에도 사용할 수 있는 공통 획득 액터입니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSGachaArtifactPickup : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSGachaArtifactPickup();
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	void InitializeArtifact(FName InArtifactId, int32 InQuantity = 1);
	void Launch(FVector Impulse);
	FName GetArtifactId() const { return ArtifactId; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UBoxComponent> InteractionBox;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UStaticMeshComponent> DisplayMesh;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gacha") FName ArtifactId;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gacha") FText ArtifactDisplayName;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gacha") int32 Quantity = 1;
};
