#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Trash/CSTrashable.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryCrateActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTexture2D;
class UCSProductVisualRegistry;
class ACSProductPickupActor;
class UPrimitiveComponent;
class UNiagaraComponent;
class ACSStoreGameState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnCrateQuantityChanged,
	int32,
	NewQuantity);

UCLASS()
class CONVENIENCESTORE_API ACSDeliveryCrateActor : public AActor,
	public ICSInteractable,
	public ICSTrashable
{
	GENERATED_BODY()

public:
	ACSDeliveryCrateActor();
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool InitializeCrate(FDataTableRowHandle InProductRow, int32 InQuantity);

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FName GetProductId() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FText GetProductDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FDataTableRowHandle GetProductRow() const;

	UFUNCTION(BlueprintPure, Category = "Delivery|Product")
	FCSProductRuntimeState GetProductRuntimeState() const
	{
		return ProductRuntimeState;
	}

	UFUNCTION(BlueprintPure, Category = "Delivery|Freshness")
	int32 GetCurrentFreshness() const;

	UFUNCTION(BlueprintPure, Category = "Delivery|Product")
	TSubclassOf<ACSProductPickupActor> GetProductActorClass() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	int32 GetRemainingQuantity() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	TSoftObjectPtr<UTexture2D> GetProductIcon() const;

	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool ConsumeOne();

	ACSProductPickupActor* SpawnProductActor() const;

	// 물리를 중지하고 CrateMesh를 Blueprint 기본 상대 Transform으로 복원합니다.
	void PrepareForPickupPhysics();

	// 플레이어가 월드 바닥에 내려놓은 배송 상자의 물리를 활성화합니다.
	void EnableWorldDropPhysics();

	void EnterFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void ExitFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void SetHeldByPlayerForFreshness(bool bIsHeldByPlayer);

	FVector GetHeldRelativeLocation() const
	{
		return HeldRelativeLocation;
	}

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FCSOnCrateQuantityChanged OnQuantityChanged;

	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnCrateDepleted();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CrateMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery")
	int32 RemainingQuantity = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery|Product")
	FCSProductRuntimeState ProductRuntimeState;

	UPROPERTY(Transient)
	int32 MatchingFreshnessSafeZoneCount = 0;

	UPROPERTY(Transient)
	bool bIsHeldByPlayerForFreshness = false;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SpoiledVFXComponent;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> SpoiledVFXStoreGameState;

	FTimerHandle SpoiledVFXRefreshTimerHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Holding")
	FVector HeldRelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Physics",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PawnPushImpulseStrength = 50.0f;

	UFUNCTION()
	void HandlePawnOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSpoiledVFXBusinessDayStarted(int32 NewBusinessDay);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Product")
	TSubclassOf<ACSProductPickupActor> ProductActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Visual")
	TObjectPtr<UCSProductVisualRegistry> VisualRegistry;

	void EnsureFreshnessRuntimeStateInitialized();
	void UpdateFreshnessDecayState();
	void CacheSpoiledVFXComponent();
	void RefreshSpoiledVFXTimer();
	void HandleSpoiledVFXRefreshTimer();
	void UpdateSpoiledVFX();
};
