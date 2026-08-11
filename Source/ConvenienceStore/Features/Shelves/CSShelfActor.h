#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSShelfActor.generated.h"

class UArrowComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

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
class CONVENIENCESTORE_API ACSShelfActor : public AActor, public ICSShelfPlaceable
{
	GENERATED_BODY()

public:
	ACSShelfActor();
	virtual void OnConstruction(const FTransform& Transform) override;

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

	// 들고 있는 상품을 놓을 수 있을 때만 진열대 외곽선 상태를 갱신합니다.
	void SetPlacementHighlight(ECSShelfPlacementPreviewState NewState);

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShelfMesh;

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

	// 외곽선 표시 전의 렌더링 설정을 보관하여 다른 기능의 Custom Depth 설정을 보존합니다.
	TMap<TObjectPtr<UPrimitiveComponent>, bool> OriginalCustomDepthStates;
	TMap<TObjectPtr<UPrimitiveComponent>, int32> OriginalCustomStencilValues;
};
