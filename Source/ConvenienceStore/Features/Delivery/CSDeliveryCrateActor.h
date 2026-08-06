#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryCrateActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTexture2D;
class UCSProductVisualRegistry;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnCrateQuantityChanged,
	int32,
	NewQuantity);

UCLASS()
class CONVENIENCESTORE_API ACSDeliveryCrateActor : public AActor
{
	GENERATED_BODY()

public:
	ACSDeliveryCrateActor();

	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool InitializeCrate(FDataTableRowHandle InProductRow, int32 InQuantity);

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FName GetProductId() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FText GetProductDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	int32 GetRemainingQuantity() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	TSoftObjectPtr<UTexture2D> GetProductIcon() const;

	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool ConsumeOne();

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FCSOnCrateQuantityChanged OnQuantityChanged;

	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnCrateDepleted();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CrateMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery")
	int32 RemainingQuantity = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Visual")
	TObjectPtr<UCSProductVisualRegistry> VisualRegistry;
};
