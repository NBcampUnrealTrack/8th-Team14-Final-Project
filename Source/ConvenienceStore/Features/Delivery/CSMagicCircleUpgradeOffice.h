#pragma once

#include "CoreMinimal.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSMagicCircleUpgradeOffice.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UCSMagicCircleUpgradeWidget;

// 건물 메시와 상호작용 위치는 자식 BP에서 지정합니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSMagicCircleUpgradeOffice : public AActor, public ICSInteractable
{
	GENERATED_BODY()
public:
	ACSMagicCircleUpgradeOffice();
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> BuildingMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> InteractionMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UBoxComponent> InteractionCollision;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MagicCircle|UI") TSubclassOf<UCSMagicCircleUpgradeWidget> UpgradeWidgetClass;
private:
	UPROPERTY(Transient) TObjectPtr<UCSMagicCircleUpgradeWidget> ActiveUpgradeWidget;
};
