#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Trash/CSTrashable.h"
#include "CSProductPickupActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UStaticMesh;
class UCSProductVisualRegistry;
class UPrimitiveComponent;
class UNiagaraComponent;
class ACSStoreGameState;
class ACSStoreEmployeeCharacter;

// 플레이어가 집어서 들 수 있는 월드 상품 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSProductPickupActor : public AActor,
	public ICSInteractable,
	public ICSTrashable
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

	// 직원 업무가 이 상품을 처리하는 동안 플레이어·다른 직원의 중복 집기를 막습니다.
	bool TryReserveForEmployee(ACSStoreEmployeeCharacter* Employee, int32 TaskId);
	bool CanBeReservedByEmployee(const ACSStoreEmployeeCharacter* Employee, int32 TaskId) const;
	void ReleaseEmployeeReservation(const ACSStoreEmployeeCharacter* Employee, int32 TaskId);
	bool IsReservedByEmployee() const;
	void ClearEmployeeReservationForPlayer() { ReservedByEmployee = nullptr; ReservationTaskId = INDEX_NONE; ReservationStartTimeSeconds = 0.0f; }

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
	// 창고에 보관된 낱개 상품을 물리 충돌 없이 조준해서 꺼낼 수 있도록 선택용 충돌만 전환합니다.
	void SetStoredForPlayerInteraction(bool bStoredForPlayerInteraction);

	// 진열대 호환성을 위해 보관 환경을 기록하지만 유통기한에는 영향을 주지 않습니다.
	void SetStorageShelfType(ECSProductShelfType InStorageShelfType);

	void EnterFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void ExitFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void SetHeldByPlayerForFreshness(bool bIsHeldByPlayer);
	void SetFreshnessFrozenForDisplay(bool bFrozen);

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

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Product|Employee")
	TObjectPtr<ACSStoreEmployeeCharacter> ReservedByEmployee = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Product|Employee")
	int32 ReservationTaskId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Product|Employee")
	float ReservationStartTimeSeconds = 0.0f;

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
	bool bIsFreshnessFrozenForDisplay = false;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SpoiledVFXComponent;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> SpoiledVFXStoreGameState;

	FTimerHandle SpoiledVFXRefreshTimerHandle;

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

	void ApplyProductVisual();
	void EnsureProductRuntimeStateInitialized();
	void UpdateFreshnessDecayState();
	void CacheSpoiledVFXComponent();
	void RefreshSpoiledVFXTimer();
	void HandleSpoiledVFXRefreshTimer();
	void UpdateSpoiledVFX();
};
