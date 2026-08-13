#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSProductPickupActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UStaticMesh;
class UCSProductVisualRegistry;
class UPrimitiveComponent;
class UNiagaraComponent;
class ACSStoreGameState;

// 플레이어가 집어서 들 수 있는 월드 상품 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSProductPickupActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSProductPickupActor();

	virtual void OnConstruction(const FTransform& Transform) override;
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

	// 현재 상품에 설정된 데이터 테이블 행 정보를 프리뷰 등 다른 표시 객체에 전달합니다.
	UFUNCTION(BlueprintPure, Category = "Product")
	FDataTableRowHandle GetProductRow() const
	{
		return ProductRow;
	}

	// 현재 상품 외형을 적용하는 DataAsset을 프리뷰 액터에 전달합니다.
	UCSProductVisualRegistry* GetVisualRegistry() const
	{
		return VisualRegistry;
	}

	// DataAsset에 외형이 없을 때 사용할 기본 메시를 프리뷰에도 동일하게 적용합니다.
	UStaticMesh* GetFallbackMesh() const
	{
		return FallbackMesh;
	}

	// 기본 메시의 상대 트랜스폼을 프리뷰에도 동일하게 적용합니다.
	FTransform GetFallbackMeshRelativeTransform() const
	{
		return FallbackMeshRelativeTransform;
	}

	UFUNCTION(BlueprintPure, Category = "Product")
	FText GetProductDisplayName() const;

	UFUNCTION(BlueprintCallable, Category = "Product")
	bool InitializeProduct(FDataTableRowHandle InProductRow);

	UFUNCTION(BlueprintPure, Category = "Product|Runtime")
	FCSProductRuntimeState GetProductRuntimeState() const
	{
		return ProductRuntimeState;
	}

	UFUNCTION(BlueprintCallable, Category = "Product|Runtime")
	void ApplyProductRuntimeState(const FCSProductRuntimeState& InProductRuntimeState);

	UFUNCTION(BlueprintPure, Category = "Product|ShelfLife")
	bool IsExpired() const;

	UFUNCTION(BlueprintPure, Category = "Product|ShelfLife")
	bool IsSellable() const;

	UFUNCTION(BlueprintPure, Category = "Product|Freshness")
	int32 GetCurrentFreshness() const;

	// 바닥 물리를 중지하고 메시를 상품별 정상 상대 Transform으로 복원합니다.
	void PrepareForPickupPhysics();

	// 플레이어가 월드 바닥에 내려놓은 상품 메시의 물리를 활성화합니다.
	void EnableWorldDropPhysics();

	// 진열대 호환성을 위해 보관 환경을 기록하지만 유통기한에는 영향을 주지 않습니다.
	void SetStorageShelfType(ECSProductShelfType InStorageShelfType);

	void EnterFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void ExitFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void SetHeldByPlayerForFreshness(bool bIsHeldByPlayer);

	// 계산대 연출처럼 실제 재고로 취급하지 않을 표시용 상품을 재고 집계에서 제외합니다.
	void SetExcludedFromStockCount(bool bShouldExclude)
	{
		bIsExcludedFromStockCount = bShouldExclude;
	}

	// 재고 서브시스템이 상품 수량을 집계해도 되는지 반환합니다.
	bool IsExcludedFromStockCount() const
	{
		return bIsExcludedFromStockCount;
	}

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ProductMesh;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Product",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Product|Visual")
	TObjectPtr<UCSProductVisualRegistry> VisualRegistry;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Product|Visual")
	TObjectPtr<UStaticMesh> FallbackMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Product|Visual")
	FTransform FallbackMeshRelativeTransform = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Product|Physics",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PawnPushImpulseStrength = 75.0f;

	// 런타임 연출용 상품은 월드에 존재해도 실제 재고 수량에 포함하지 않습니다.
	UPROPERTY(Transient)
	bool bIsExcludedFromStockCount = false;

	// 현재 상품이 놓인 시설의 보관 환경입니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Product|ShelfLife")
	ECSProductShelfType StorageShelfType = ECSProductShelfType::UtilityShelf;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Product|Runtime")
	FCSProductRuntimeState ProductRuntimeState;

	UPROPERTY(Transient)
	int32 MatchingFreshnessSafeZoneCount = 0;

	UPROPERTY(Transient)
	bool bIsHeldByPlayerForFreshness = false;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SpoiledVFXComponent;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> SpoiledVFXStoreGameState;

	UFUNCTION()
	void HandlePawnOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSpoiledVFXGameTimeChanged(float NewGameMinutes);

	UFUNCTION()
	void HandleSpoiledVFXBusinessDayStarted(int32 NewBusinessDay);

	void ApplyProductVisual();
	void EnsureProductRuntimeStateInitialized();
	void UpdateFreshnessDecayState();
	void CacheSpoiledVFXComponent();
	void UpdateSpoiledVFX();
};
