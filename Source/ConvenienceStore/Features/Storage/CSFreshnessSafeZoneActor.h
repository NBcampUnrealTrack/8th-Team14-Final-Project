#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSFreshnessSafeZoneActor.generated.h"

class UBoxComponent;
class USceneComponent;
class UPrimitiveComponent;
class UArrowComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class ACSDeliveryCrateActor;
class ACSProductPickupActor;

UCLASS()
class CONVENIENCESTORE_API ACSFreshnessSafeZoneActor : public AActor,
	public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSFreshnessSafeZoneActor();

	// 플레이어가 손에 든 상자 또는 낱개 상품을 현재 창고의 적절한 슬롯에 보관합니다.
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	// 이 포인트가 지정 상품 보관 타입의 배송 상자를 받을 수 있는지 확인합니다.
	bool CanStoreCrate(const ACSDeliveryCrateActor* Crate) const;
	// 직원이 이 위치까지 이동할 수 있도록 NavMesh에 맞춘 접근 지점을 반환합니다.
	bool TryGetStorageApproachPoint(FVector& OutLocation) const;
	// 배송 상자를 다음 빈 보관 슬롯에 고정하여, 실제 창고 재고로 보관합니다.
	bool TryStoreCrate(ACSDeliveryCrateActor* Crate);
	// 플레이어가 조준한 상자 슬롯에만 배송 상자를 적재합니다.
	bool TryStoreCrateInSlot(
		ACSDeliveryCrateActor* Crate,
		const UPrimitiveComponent* SlotInteractionComponent);
	// 조준한 상자 슬롯의 보관 상자를 꺼내기 전에 점유·신선도 등록을 해제합니다.
	bool TryTakeCrateFromSlot(
		const UPrimitiveComponent* SlotInteractionComponent,
		ACSDeliveryCrateActor*& OutCrate);
	// 조준한 상자 슬롯에 보관된 상자를 변경 없이 조회하여, 플레이어 HUD에 상품 정보를 표시합니다.
	bool TryGetStoredCrateFromSlot(
		const UPrimitiveComponent* SlotInteractionComponent,
		ACSDeliveryCrateActor*& OutCrate) const;
	// 현재 조준한 컴포넌트가 상자 슬롯인지와 해당 슬롯 번호를 반환합니다.
	bool TryGetCrateStorageSlotIndex(
		const UPrimitiveComponent* SlotInteractionComponent,
		int32& OutSlotIndex) const;
	// 조준한 상자 슬롯의 실제 적재 Transform을 반환하여 플레이어 프리뷰와 실제 보관 위치를 일치시킵니다.
	bool TryGetCrateStorageSlotWorldTransform(
		const UPrimitiveComponent* SlotInteractionComponent,
		FTransform& OutTransform) const;
	// 해당 슬롯에 상자를 보관할 수 있는지 미리 확인하여 프리뷰를 잘못 표시하지 않습니다.
	bool CanStoreCrateInSlot(
		const ACSDeliveryCrateActor* Crate,
		const UPrimitiveComponent* SlotInteractionComponent) const;
	bool IsLooseProductDepositComponent(const UPrimitiveComponent* InteractionComponent) const;
	// 다음 낱개 상품 자동 격자의 Transform을 반환하여 프리뷰 위치를 실제 보관 위치와 맞춥니다.
	bool TryGetNextLooseProductStorageSlotWorldTransform(FTransform& OutTransform) const;
	// 창고 슬롯에서 배송 상자와 동일한 외형의 고스트를 표시하거나 숨깁니다.
	void ShowCrateStoragePreview(
		const ACSDeliveryCrateActor* SourceCrate,
		const FTransform& SlotWorldTransform,
		bool bCanStore);
	void HideCrateStoragePreview();
	// 저장된 상자를 다시 운반할 때 점유 슬롯을 해제합니다.
	void RemoveStoredCrate(ACSDeliveryCrateActor* Crate);
	// 낱개 상품을 보관 구역의 다음 자동 격자 위치에 고정할 수 있는지 확인합니다.
	bool CanStoreLooseProduct(const ACSProductPickupActor* Product) const;
	// 낱개 상품을 보관 구역 안의 빈 자동 격자 슬롯에 정리해 보관합니다.
	bool TryStoreLooseProduct(ACSProductPickupActor* Product);
	// 보관된 낱개 상품을 다시 진열에 사용할 때 점유 슬롯과 보관 효과를 해제합니다.
	void RemoveStoredLooseProduct(ACSProductPickupActor* Product);

	UFUNCTION(BlueprintPure, Category = "Storage")
	ECSProductShelfType GetStorageType() const { return SafeStorageType; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SafeZoneBox;

	// 직원이 창고 앞에서 멈출 NavMesh 접근 마커입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> StorageApproachPoint;

	// 배송 상자를 하나씩 배치할 수 있는 편집용 화살표 슬롯입니다. BP 컴포넌트 트리에서 직접 옮겨 배치하실 수 있습니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TArray<TObjectPtr<UArrowComponent>> CrateStorageSlotPoints;

	// 각 Arrow 슬롯을 조준해 상자를 넣거나 꺼낼 수 있도록, Arrow에 종속된 상호작용 박스입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TArray<TObjectPtr<UBoxComponent>> CrateStorageSlotInteractionBoxes;

	// 창고마다 활성화할 상자 보관 슬롯 수입니다. 이 창고는 최대 9개 슬롯을 제공합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Crates", meta = (ClampMin = "1", ClampMax = "9", UIMin = "1", UIMax = "9"))
	int32 CrateStorageCapacity = 9;

	// 낱개 상품을 정리할 구역입니다. Box 크기와 슬롯 간격으로 자동 격자 수량이 정해집니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> LooseProductStorageZone;

	// 낱개 상품을 자동 격자 보관소에 넣을 때 조준할 별도 상호작용 영역입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> LooseProductDepositPoint;

	// 상자 적재 전에 실제 상자와 동일한 메시를 반투명 고스트로 보여 주는 표시 전용 메시입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CrateStoragePreviewMesh;

	// 상자 고스트에 적용할 반투명 프리뷰 머티리얼입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage|Placement Preview")
	TObjectPtr<UMaterialInterface> ValidCrateStoragePreviewMaterial;

	// 보관할 수 없는 창고·슬롯을 조준했을 때 사용할 빨간색 고스트 머티리얼입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage|Placement Preview")
	TObjectPtr<UMaterialInterface> InvalidCrateStoragePreviewMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Freshness")
	ECSProductShelfType SafeStorageType = ECSProductShelfType::ChilledShelf;

	// 일반·냉장·냉동 보관소 모두 동일한 구조를 사용하되, 필요 없는 포인트는 자동 적재에서 제외할 수 있습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Employee")
	bool bAllowEmployeeStorage = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Storage|Employee")
	TArray<TObjectPtr<ACSDeliveryCrateActor>> StoredCrates;

	// 낱개 상품 보관은 상자 보관보다 우선순위가 낮으며, 필요하지 않은 창고에서는 끌 수 있습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Employee")
	bool bAllowEmployeeLooseProductStorage = true;

	// 상품 메시 크기에 맞춰 간격을 조정하시면 됩니다. Box Extent와 함께 자동 보관 가능 수량을 결정합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage|Employee", meta = (ClampMin = "1.0", UIMin = "1.0"))
	FVector LooseProductSlotSpacing = FVector(45.0f, 45.0f, 35.0f);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Storage|Employee")
	TArray<TObjectPtr<ACSProductPickupActor>> StoredLooseProducts;

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
	int32 GetCrateStorageCapacity() const;
	void RefreshCrateStorageSlotAvailability();
	bool IsSupportedSafeStorageType() const;
	bool IsStorageTypeCompatible(const ACSDeliveryCrateActor* Crate) const;
	bool IsStorageTypeCompatible(const ACSProductPickupActor* Product) const;
	bool IsProductRowCompatible(const FDataTableRowHandle& ProductRow) const;
	int32 FindEmptyStorageSlot() const;
	bool CanStoreCrateAtIndex(const ACSDeliveryCrateActor* Crate, int32 SlotIndex) const;
	bool StoreCrateAtSlot(ACSDeliveryCrateActor* Crate, int32 SlotIndex);
	int32 FindEmptyLooseProductStorageSlot() const;
	int32 GetLooseProductStorageCapacity() const;
	FTransform GetLooseProductStorageSlotWorldTransform(int32 SlotIndex) const;
	void RegisterOverlappingActor(AActor* OtherActor);
	void UnregisterOverlappingActor(AActor* OtherActor);

	TSet<TWeakObjectPtr<AActor>> RegisteredActors;
};
