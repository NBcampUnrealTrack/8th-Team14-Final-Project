#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSProductPickupActor.generated.h"

class UStaticMeshComponent;

// 플레이어가 집어서 들 수 있는 월드 상품 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSProductPickupActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSProductPickupActor();

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	UFUNCTION(BlueprintPure, Category = "Product")
	FName GetProductId() const
	{
		if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
		{
			return NAME_None;
		}

		return ProductRow.RowName;
	}

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ProductMesh;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Product",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	FText DisplayName;
};
