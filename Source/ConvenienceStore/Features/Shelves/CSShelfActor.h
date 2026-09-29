#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityTypes.h"
#include "Engine/DataTable.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSShelfActor.generated.h"

class UArrowComponent;
class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class ACSProductPickupActor;
class ACSDeliveryCrateActor;
class UCShelfConfigurationWidget;
class APlayerController;
class UCSSaveIdComponent;
class UDataTable;
struct FCSShelfRecord;
struct FCSProductRuntimeState;

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

// 한 진열 단의 품목 고정 여부를 저장합니다. 플레이어가 직접 지정한 단은 다른 단과 같은 상품도 허용합니다.
USTRUCT(BlueprintType)
struct FCSShelfLevelConfiguration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Configuration")
	ECSShelfSlotLevel SlotLevel = ECSShelfSlotLevel::Middle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Configuration",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	// 플레이어가 UI에서 지정한 단은 다른 단과 같은 품목을 명시적으로 설정할 수 있습니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shelf|Configuration")
	bool bIsPlayerConfigured = false;
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
	// 조준한 단의 뒤쪽 슬롯부터 회수 가능한 상품을 찾습니다. 다른 단으로 넘어가지 않습니다.
	ACSProductPickupActor* FindRetrievableProduct(FVector AimPoint, const FDataTableRowHandle& RequiredRow, const AActor* Collector = nullptr) const;
	int32 GetProductCountOnAimedLevel(FVector AimPoint) const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	// 들고 있는 상품의 현재 배치 상태를 진열대 외곽선으로 표시합니다.
	void SetPlacementHighlight(ECSShelfPlacementPreviewState NewState);
	// 배치 안내가 끝났을 때 기존 CustomDepth 상태로 되돌립니다.
	void ClearPlacementHighlight();
	// 플레이어가 조준한 단에 배치하지 못한 정확한 원인을 HUD에 표시합니다.
	FText GetPlayerPlacementFailureText(
		const ACSProductPickupActor* Product,
		FVector PlacementHint) const;

	// 조준 위치와 무관하게 진열대 전체에 배치 가능한 슬롯이 있는지 기준으로 외곽선 상태를 반환합니다.
	ECSShelfPlacementPreviewState GetPlacementHighlightState(AActor* ItemActor) const;

	// CustomerShoppingPoint를 NavMesh 바닥 높이에 맞춘 공통 접근 위치를 반환합니다.
	// 손님과 진열 직원이 동일한 위치를 사용하므로, 건축 모드의 BrowseZoneBox 검증 범위를 벗어나지 않습니다.
	bool TryGetCustomerShoppingPoint(FVector& OutLocation) const;

	// 손님 접근 마커를 기준으로 만든 도착 판정 영역을 반환
	bool TryGetBrowseZone(FVector& OutCenter, FRotator& OutFacing, float& OutRadius) const;

	// 월드 좌표가 도착 판정 영역 안인지 확인합니다.
	bool IsPointInsideBrowseZone(const FVector& WorldPoint) const;

	// 상품이 이 진열대에 배치될 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Shelf")
	FCSOnItemPlaced OnItemPlaced;

	// 현재 비어 있는 진열 슬롯 수를 반환하여 직원 업무 생성에 사용합니다.
	UFUNCTION(BlueprintPure, Category = "Shelf|Employee")
	int32 GetEmptySlotCount() const;

	// 지정 상품을 현재 단별 구성 규칙에 따라 직원이 실제로 채울 수 있는 빈 슬롯 수입니다.
	// 전체 빈 슬롯 수와 다를 수 있으므로, 상자 진열 업무의 요청 수량 계산에 사용합니다.
	int32 GetEmployeeRestockableSlotCount(
		const FDataTableRowHandle& ProductRow,
		bool bAllowCrossTierDuplicateFallback) const;

	// 계산대·장식 선반처럼 ACSShelfActor를 상속하지만 직원 자동 보충 대상이 아니어야 하는 시설을 구분합니다.
	UFUNCTION(BlueprintPure, Category = "Shelf|Employee")
	bool CanEmployeeRestock() const { return bAllowEmployeeRestock && bIsInstalled; }

	// 시설 데이터 또는 진열대 설정으로 결정된 현재 보관 타입을 직원 업무 시스템에 제공합니다.
	UFUNCTION(BlueprintPure, Category = "Shelf|Employee")
	ECSProductShelfType GetShelfStorageType() const { return ShelfType; }

	// 자동 진열과 진열 구성 UI가 공통으로 사용할 상품군 호환 규칙입니다. 수동 진열의 경고 여부와는 별도로 판단합니다.
	UFUNCTION(BlueprintPure, Category = "Shelf|Configuration")
	bool IsProductShelfTypeCompatible(ECSProductShelfType ProductShelfType) const;

	// 기존 진열 상품 또는 초기 진열 설정을 기준으로, 직원이 채워야 할 상품 행을 반환합니다.
	bool GetRestockProductRow(FDataTableRowHandle& OutProductRow) const;
	// 지정 상품이 현재 진열대의 어느 단에 들어갈 수 있는지 직원 업무 생성 단계에서 확인합니다.
	bool CanEmployeeRestockProduct(
		const FDataTableRowHandle& ProductRow,
		bool bAllowCrossTierDuplicateFallback) const;

	// 배송 상자에서 실제 상품을 생성해 빈 슬롯에 배치하고, 성공한 수량만 상자에서 차감합니다.
	int32 TryRestockFromCrate(ACSDeliveryCrateActor* SourceCrate, int32 RequestedQuantity);

	// 직원이 손에 든 상품을 다음 빈 슬롯에 정확히 놓을 수 있도록 배치 기준 위치를 반환합니다.
	bool GetNextEmployeeRestockSlotLocation(
		const FDataTableRowHandle& ProductRow,
		bool bAllowCrossTierDuplicateFallback,
		FVector& OutSlotLocation) const;
	// 직원이 선택된 규칙에 맞는 단의 빈 슬롯에 상품을 놓습니다.
	bool TryPlaceEmployeeProduct(
		ACSProductPickupActor* Product,
		bool bAllowCrossTierDuplicateFallback);

	// E 상품 상호작용과 분리해, 플레이어가 F 입력으로만 진열 구성 UI를 열 수 있도록 제공합니다.
	bool OpenShelfConfiguration(AActor* Interactor);

	UFUNCTION(BlueprintPure, Category = "Shelf|Configuration")
	TArray<FCSShelfLevelConfiguration> GetLevelConfigurations() const { return LevelConfigurations; }

	// ----- 세이브 / 로드 -----
	// 슬롯과 단 설정이 protected이고 설치 여부가 Transient이므로 진열대가 직접 기록·복원

	// 식별자·위치·단 설정·슬롯 상품을 세이브 레코드에 기록
	// 상품이 없는 슬롯은 기록하지 않음
	void CaptureSaveRecord(FCSShelfRecord& OutRecord) const;

	// 세이브 레코드로 단 설정과 설치 상태를 복원하고 기록된 슬롯에만 상품을 다시 생성
	void RestoreSaveRecord(const FCSShelfRecord& InRecord, const UDataTable* FallbackProductTable);

	// 모든 슬롯의 상품을 점유 해제 후 제거
	// BeginPlay의 SpawnInitialProducts()가 채운 초기 상품의 중복을 방지
	void ClearAllSlotsForRestore();

	// 플레이어가 단을 지정하지 않은 상태에서 상품 보관 타입이 맞지 않아 직원 정리가 필요한지 확인합니다.
	bool ShouldEmployeeReorganizeProduct(const ACSProductPickupActor* Product) const;

	UFUNCTION(BlueprintCallable, Category = "Shelf|Configuration")
	bool SetLevelProductConfiguration(
		ECSShelfSlotLevel SlotLevel,
		FDataTableRowHandle ProductRow);

	// 현재 단 설정과 다른 상품 수입니다. 설정 변경 뒤 UI가 직원 정리 대기 수를 안내할 때 사용합니다.
	UFUNCTION(BlueprintPure, Category = "Shelf|Configuration")
	int32 GetMismatchedProductCountOnLevel(ECSShelfSlotLevel SlotLevel) const;

	UFUNCTION(BlueprintCallable, Category = "Shelf|Configuration")
	bool ClearLevelProductConfiguration(ECSShelfSlotLevel SlotLevel);

protected:
	// 조준 위치와 가장 가까운 상단·중단·하단을 반환합니다. 이미 찬 슬롯도 단 판정에는 사용합니다.
	ECSShelfSlotLevel FindAimedSlotLevel(FVector PlacementHint) const;
	// 조준한 단 안에서만 가장 가까운 빈 슬롯을 반환합니다.
	int32 FindClosestEmptySlot(FVector PlacementHint) const;
	// 빈칸이 없을 때에도 실패 프리뷰를 실제 조준 단의 슬롯에 고정하기 위한 공통 검색입니다.
	int32 FindClosestSlotOnAimedLevel(FVector PlacementHint, bool bRequireEmptySlot) const;
	int32 FindEmptySlotForProduct(
		const FDataTableRowHandle& ProductRow,
		bool bAllowCrossTierDuplicateFallback,
		ECSShelfSlotLevel PreferredLevel,
		bool bIgnoreStorageCompatibility = false,
		bool bAllowOtherLevelFallback = true) const;
	// 직원은 플레이어가 지정한 같은 상품의 단을 먼저 채우고, 그 단이 가득 찬 뒤에만 기본 순서를 사용합니다.
	int32 FindEmptyEmployeeRestockSlot(
		const FDataTableRowHandle& ProductRow,
		bool bAllowCrossTierDuplicateFallback) const;
	bool CanPlaceProductInLevel(
		const FDataTableRowHandle& ProductRow,
		ECSShelfSlotLevel SlotLevel,
		bool bAllowCrossTierDuplicateFallback,
		bool bIgnoreStorageCompatibility = false) const;
	bool IsProductRowEqual(const FDataTableRowHandle& Left, const FDataTableRowHandle& Right) const;
	bool GetProductRowOnLevel(ECSShelfSlotLevel SlotLevel, FDataTableRowHandle& OutProductRow) const;
	const FCSShelfLevelConfiguration* FindLevelConfiguration(ECSShelfSlotLevel SlotLevel) const;
	FCSShelfLevelConfiguration* FindLevelConfiguration(ECSShelfSlotLevel SlotLevel);
	void EnsureLevelConfigurations();
	bool IsProductCompatibleWithShelf(const FDataTableRowHandle& ProductRow) const;
	// 실제 부착·충돌·점유 처리를 한곳에 모아 플레이어와 직원이 같은 배치 결과를 사용하게 합니다.
	bool PlaceItemInSlot(AActor* ItemActor, int32 SlotIndex);
	// 세이브 복원 전용으로 지정 슬롯에 상품을 생성하고 런타임 상태를 적용
	// 단 설정 규칙을 거치지 않고 저장된 슬롯 번호에 그대로 배치
	bool RestoreProductInSlot(
		const FDataTableRowHandle& InProductRow,
		int32 SlotIndex,
		const FCSProductRuntimeState& InRuntimeState,
		TSubclassOf<ACSProductPickupActor> InProductClass);
	bool OpenShelfConfigurationWidget(AActor* Interactor);
	UFUNCTION()
	void HandleShelfConfigurationClosed();
	// Arrow Component 이름에서 Slot_00 형식의 슬롯 번호를 읽어옵니다.
	bool TryGetSlotMarkerIndex(const UArrowComponent* SlotMarker, int32& OutSlotIndex) const;
	// 블루프린트에서 배치한 슬롯 마커를 현재 슬롯 배열에 반영합니다.
	void SyncSlotsFromMarkers();
	// 시설 DataTable 행에서 현재 진열대가 제공하는 보관 환경을 읽어옵니다.
	void SyncFacilityData();
	// 마커와 반경 변경을 접근 영역 박스에 반영합니다.
	void SyncBrowseZoneBox();
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

	// 세이브 복원 시 이 진열대를 다시 찾기 위한 안정 식별자
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCSSaveIdComponent> SaveIdComponent;

	// 개별 가구 블루프린트에서 메시 높이를 보정할 수 있도록 SceneRoot의 자식으로 둡니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShelfMesh;

	// 진열대 위에 NavMesh가 생성되지 않도록 막는 Box
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> NavObstacleBox;

	// 손님이 진열대에 설 자리를 가리키는 마커입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> CustomerShoppingPoint;

	// 손님이 설 자리를 건축 판정에 알리기 위한 표시용 박스
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> BrowseZoneBox;

	// 마커를 중심으로 만드는 도착 판정 영역의 반경
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Customer",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float BrowseZoneRadius = 75.0f;

	// 건축 판정에서 세로로 볼 범위입니다. 층이 다른 시설을 구분하는 데만 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Customer",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float BrowseZoneHeight = 200.0f;

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

	// true인 진열대만 진열 직원이 빈 슬롯과 재고 상자를 검사하여 자동 보충합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Employee")
	bool bAllowEmployeeRestock = true;

	// 진열대 블루프린트에서 상품 위치마다 하나씩 추가합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf")
	TArray<FCSShelfSlot> Slots;

	// 상단·중단·하단의 플레이어 지정 품목을 따로 보관해 한 단 안에서는 서로 다른 상품이 섞이지 않게 합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf|Configuration")
	TArray<FCSShelfLevelConfiguration> LevelConfigurations;

	// 진열대를 조준하고 상호작용할 때 열 진열 구성 관리 UI입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shelf|Configuration")
	TSubclassOf<UCShelfConfigurationWidget> ShelfConfigurationWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UCShelfConfigurationWidget> ActiveShelfConfigurationWidget;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> ShelfConfigurationPlayerController;

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
