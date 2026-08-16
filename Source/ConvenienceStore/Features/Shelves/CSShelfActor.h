#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityTypes.h"
#include "Engine/DataTable.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSShelfActor.generated.h"

class UArrowComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class ACSProductPickupActor;

// 상품이 이 진열대의 슬롯에 실제로 배치되었을 때 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnItemPlaced,
	AActor*,
	PlacedItem,
	int32,
	SlotIndex);

USTRUCT(BlueprintType)
struct FCSInitialShelfProduct
{
	GENERATED_BODY()

	// 상품 자동 진열을 시작할 슬롯 번호입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Initial Products",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 StartSlotIndex = 0;

	// 게임 시작 시 생성할 상품의 DataTable 행입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Initial Products",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	// 1이면 기본형, 2 이상이면 시작 슬롯부터 빈 슬롯을 순서대로 채웁니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Initial Products",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 Quantity = 1;
};

USTRUCT(BlueprintType)
struct FCSShelfSlot
{
	GENERATED_BODY()

	// 진열대 액터를 기준으로 한 상품의 위치와 회전입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf")
	FTransform LocalTransform;

	// 손님 AI가 진열 위치의 접근성과 상품 선택 규칙에 사용할 슬롯 높이입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf")
	ECSShelfSlotLevel SlotLevel = ECSShelfSlotLevel::Middle;

	// 현재 이 슬롯을 사용 중인 상품을 실행 중에만 저장합니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shelf")
	TObjectPtr<AActor> OccupyingItem = nullptr;
};

// 설정된 슬롯 위치에 상품을 정확하게 배치하는 진열대 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSShelfActor : public AActor,
	public ICSShelfPlaceable,
	public ICSBuildableFacility
{
	GENERATED_BODY()

public:
	ACSShelfActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	virtual bool CanPlaceItem_Implementation(AActor* ItemActor) const override;
	virtual bool GetPlacementPreview_Implementation(
		AActor* ItemActor,
		FVector PlacementHint,
		FTransform& OutSlotWorldTransform) const override;
	virtual ECSShelfPlacementPreviewState GetPlacementPreviewState_Implementation(
		AActor* ItemActor,
		FVector PlacementHint,
		FTransform& OutSlotWorldTransform) const override;
	virtual bool TryPlaceItem_Implementation(
		AActor* ItemActor,
		FVector PlacementHint) override;
	virtual void RemovePlacedItem_Implementation(AActor* ItemActor) override;
	virtual bool GetItemSlotLevel_Implementation(
		AActor* ItemActor,
		ECSShelfSlotLevel& OutSlotLevel) const override;
	virtual FName GetFacilityId_Implementation() const override;
	virtual bool IsFacilityInstalled_Implementation() const override;
	virtual void InitializeAsUninstalledFacility_Implementation() override;
	virtual void PrepareForBuildPickup_Implementation() override;
	virtual void FinalizeBuildPlacement_Implementation() override;

	// 들고 있는 상품을 놓을 수 있을 때만 진열대 외곽선 상태를 갱신합니다.
	void SetPlacementHighlight(ECSShelfPlacementPreviewState NewState);

	// 선택적 손님 접근 마커가 있으면 해당 컴포넌트의 월드 위치를 반환합니다.
	bool TryGetCustomerBrowseLocation(FVector& OutLocation) const;

	// 상품이 이 진열대에 배치될 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Shelf")
	FCSOnItemPlaced OnItemPlaced;

protected:
	int32 FindClosestEmptySlot(FVector PlacementHint) const;
	// Arrow Component 이름에서 Slot_00 형식의 슬롯 번호를 읽어옵니다.
	bool TryGetSlotMarkerIndex(const UArrowComponent* SlotMarker, int32& OutSlotIndex) const;
	// 블루프린트에서 배치한 슬롯 마커를 현재 슬롯 배열에 반영합니다.
	void SyncSlotsFromMarkers();
	// 시설 DataTable 행에서 현재 진열대가 제공하는 보관 환경을 읽어옵니다.
	void SyncFacilityData();
	// 냉장·냉동 상품이 현재 시설의 보관 환경과 맞지 않는지 확인합니다.
	bool ShouldWarnStorage(AActor* ItemActor) const;
	// 외곽선 표시를 끌 때 기존 Custom Depth 설정을 복원합니다.
	void RestorePlacementHighlight();
	// 레벨에서 지정한 시작 진열 상품을 게임 시작 시 한 번만 생성합니다.
	void SpawnInitialProducts();
	// 지정 슬롯에 상품 하나를 생성하고 기존 진열 배치 규칙으로 배치합니다.
	bool SpawnInitialProductInSlot(
		const FDataTableRowHandle& ProductRow,
		int32 SlotIndex);

	// 가구 이동 모드가 바닥에 맞출 공통 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 개별 가구 블루프린트에서 메시 높이를 보정할 수 있도록 SceneRoot의 자식으로 둡니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShelfMesh;

	// 미설치 물리를 켜기 직전의 Blueprint 상대 위치·회전·스케일을 보존합니다.
	UPROPERTY(Transient)
	FTransform ShelfMeshRelativeTransformBeforePhysics = FTransform::Identity;

	UPROPERTY(Transient)
	bool bHasCachedShelfMeshRelativeTransform = false;

	// 기존 레벨 배치 시설은 설치 완료 상태로 시작합니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Facility|Build")
	bool bIsInstalled = true;

	// DT_FantasyFacilityMaster의 시설 행입니다. 비어 있으면 아래 ShelfType 설정을 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Facility",
		meta = (RowType = "/Script/ConvenienceStore.CSFacilityDataRow"))
	FDataTableRowHandle FacilityRow;

	// DataTable 행이 아직 지정되지 않은 기존 진열대에서 사용할 보관 환경입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Facility")
	ECSProductShelfType ShelfType = ECSProductShelfType::UtilityShelf;

	// 진열대 블루프린트에서 상품 위치마다 하나씩 추가합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf")
	TArray<FCSShelfSlot> Slots;

	// 시작 진열 상품에 사용할 공통 상품 액터 클래스입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Initial Products")
	TSubclassOf<ACSProductPickupActor> InitialProductActorClass;

	// 레벨에서 설정한 상품을 플레이 시작 시 자동으로 진열합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Initial Products")
	TArray<FCSInitialShelfProduct> InitialProducts;

	// 외곽선 표시 전의 렌더링 설정을 보관하여 다른 기능의 Custom Depth 설정을 보존합니다.
	TMap<TObjectPtr<UPrimitiveComponent>, bool> OriginalCustomDepthStates;
	TMap<TObjectPtr<UPrimitiveComponent>, int32> OriginalCustomStencilValues;

	void SetFacilityPhysicsEnabled(bool bEnabled);
};
