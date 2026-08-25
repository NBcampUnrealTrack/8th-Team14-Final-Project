#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Character.h"
#include "CSPlayerCharacter.generated.h"

class UCameraComponent;
class UBoxComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;
class ACSCartActor;
class ACSDeliveryCrateActor;
class ACSMagicWandActor;
class ACSProductPickupActor;
class ACSProductPlacementPreviewActor;
class ACSUsableToolActor;
class UCSQuickbarInventoryComponent;
struct FInputActionValue;
struct FCSProductRuntimeState;

UENUM(BlueprintType)
enum class ECSPlayerMode : uint8
{
	Normal,
	Build
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnBuildModeChanged,
	bool,
	bIsBuildMode);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnBuildableFacilityPlaced,
	AActor*,
	FacilityActor);

UCLASS()
class CONVENIENCESTORE_API ACSPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// 플레이어 캐릭터의 기본 컴포넌트와 이동 값을 설정합니다.
	ACSPlayerCharacter();

	// 상호작용한 물체를 플레이어 손 위치에 부착합니다.
	bool TryPickUp(AActor* PickupActor);

	UFUNCTION(BlueprintPure, Category = "Player|Mode")
	bool IsBuildMode() const
	{
		return CurrentPlayerMode == ECSPlayerMode::Build;
	}

	// 튜토리얼 전용 동작을 넣지 않고, 건축 모드 전환 성공 사실만 범용으로 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Player|Build")
	FCSOnBuildModeChanged OnBuildModeChanged;

	// 시설의 실제 배치가 완료된 경우에만 범용 성공 이벤트를 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Player|Build")
	FCSOnBuildableFacilityPlaced OnBuildableFacilityPlaced;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;


protected:
	// 플레이어 시점을 제공하는 1인칭 카메라입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	// 손에 든 물체를 카메라 앞에 부착하는 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> HeldItemAnchor;

	// 수레 손잡이가 따라갈 플레이어 앞쪽의 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> CartPushAnchor;

	// 보관 중인 도구를 월드에서 숨겨 둘 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> QuickbarStorageAnchor;

	// 장착형 도구만 보관하는 확장 가능한 퀵바 인벤토리입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UCSQuickbarInventoryComponent> QuickbarInventory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BuildPlacementPreviewMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<AActor> HeldActor;

	// 현재 손잡이를 잡고 밀고 있는 수레입니다. 손에 드는 HeldActor와는 분리합니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cart")
	TObjectPtr<ACSCartActor> PushedCart;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Player|Mode")
	ECSPlayerMode CurrentPlayerMode = ECSPlayerMode::Normal;

	UPROPERTY(Transient)
	FRotator HeldBuildableFacilityRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	bool bBuildRotateLeftHeld = false;

	UPROPERTY(Transient)
	bool bBuildRotateRightHeld = false;

	UPROPERTY(Transient)
	float BuildRotateLeftHeldSeconds = 0.0f;

	UPROPERTY(Transient)
	float BuildRotateRightHeldSeconds = 0.0f;

	UPROPERTY(Transient)
	float BuildRotationAccumulator = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HeldBuildableSourceMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> HeldBuildableSourceMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BuildPlacementPreviewMeshes;

	UPROPERTY(Transient)
	TArray<FTransform> HeldBuildableSourceRelativeTransforms;

	UPROPERTY(Transient)
	FVector CachedBuildPlacementLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator CachedBuildPlacementRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	bool bHasBuildPlacementPreview = false;

	UPROPERTY(Transient)
	bool bCanPlaceHeldBuildableFacility = false;

	UPROPERTY(Transient)
	bool bIsHeldBuildableInsidePlacementArea = true;

	UPROPERTY(Transient)
	bool bHasHeldBuildablePlacementOverlap = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> HeldBuildableStateActors;

	UPROPERTY(Transient)
	TArray<uint8> HeldBuildableHiddenStates;

	UPROPERTY(Transient)
	TArray<uint8> HeldBuildableCollisionStates;

	// 들고 있는 상품의 외형을 진열대 슬롯에 표시할 프리뷰 액터 클래스입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Placement Preview")
	TSubclassOf<ACSProductPlacementPreviewActor> PlacementPreviewActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|Placement Preview")
	TObjectPtr<UMaterialInterface> BuildPlacementValidMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|Placement Preview")
	TObjectPtr<UMaterialInterface> BuildPlacementInvalidMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|Placement Preview",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WallSnapSearchDistance = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|Rotation",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BuildRotationHoldDelay = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|Rotation",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float BuildRotationDegreesPerSecond = 90.0f;

	// 현재 생성되어 있는 표시 전용 프리뷰 액터입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSProductPlacementPreviewActor> PlacementPreviewActor;

	// 현재 배송 상자 적재 프리뷰를 표시 중인 수레입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSCartActor> CartPlacementPreviewTarget;

	// 에디터에서 만든 입력 에셋을 연결합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	// Ctrl 입력으로 웅크리기 상태를 시작하고 해제하는 액션입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> DropAction;

	// F 입력으로 수레 손잡이를 잡거나 바닥에 내려놓습니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleCartPushAction;

	// 숫자키로 퀵바 슬롯을 직접 선택하는 입력입니다. 처음에는 1, 2만 연결합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Quickbar")
	TObjectPtr<UInputAction> SelectQuickbarSlot1Action;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Quickbar")
	TObjectPtr<UInputAction> SelectQuickbarSlot2Action;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Quickbar")
	TObjectPtr<UInputAction> SelectQuickbarSlot3Action;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Quickbar")
	TObjectPtr<UInputAction> SelectQuickbarSlot4Action;

	// 마우스 휠로 다음·이전 퀵바 슬롯을 순환 선택합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Quickbar")
	TObjectPtr<UInputAction> CycleQuickbarNextAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Quickbar")
	TObjectPtr<UInputAction> CycleQuickbarPreviousAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleBuildModeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RotateBuildLeftAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RotateBuildRightAction;

	// 좌클릭으로 현재 들고 있는 도구의 주 사용 기능을 실행합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PrimaryUseAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float InteractionDistance = 750.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float DropDistance = 450.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float DropTraceDepth = 1200.0f;


private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void StartCrouch();
	void StopCrouch();
	void TryInteract();
	void DropHeldItem();
	void ToggleCartPush();
	void SelectQuickbarSlot1();
	void SelectQuickbarSlot2();
	void SelectQuickbarSlot3();
	void SelectQuickbarSlot4();
	void SelectNextQuickbarSlot();
	void SelectPreviousQuickbarSlot();
	bool TrySelectQuickbarSlot(int32 SlotIndex);
	void EquipSelectedQuickbarTool();
	void StoreEquippedQuickbarTool();
	void UpdateQuickbarHUD();

	UFUNCTION()
	void HandleQuickbarChanged();
	UFUNCTION()
	void HandleQuickbarSelectionChanged(int32 SelectedSlotIndex);
	void ToggleBuildMode();
	void StartRotateBuildLeft();
	void StopRotateBuildLeft();
	void StartRotateBuildRight();
	void StopRotateBuildRight();
	void ApplyBuildRotationStep(float YawDelta);
	void UpdateBuildRotationInput(float DeltaTime);
	void ResetBuildRotationInput();
	void TryUsePrimaryTool();
	void UpdateHeldMagicWandDockHUD();
	bool IsAimingAtMagicWandHomeDock(const ACSMagicWandActor* MagicWand) const;
	bool RestoreMagicWandToHomeDock(ACSMagicWandActor* MagicWand);
	void RestoreFacilityMagicWands(AActor* FacilityActor);
	void UpdateInteractionHUD();
	void UpdateBuildModeHUD();
	void UpdatePlacementPreview();
	void UpdateBuildPlacementPreview();
	void ConfigureBuildPlacementPreview(AActor* FacilityActor);
	void ClearBuildPlacementPreview();
	void ApplyBuildPlacementPreviewMaterial(bool bIsPlacementValid);
	void SetBuildPlacementPreviewVisible(bool bIsVisible);
	void UpdateBuildPreviewComponentTransforms(
		const FVector& FacilityLocation,
		const FRotator& FacilityRotation) const;
	bool CalculateCombinedBuildPreviewBounds(FBox& OutBounds) const;
	bool AlignBuildPreviewToFloor(
		FVector& InOutFacilityLocation,
		const FRotator& FacilityRotation,
		float FloorZ) const;
	bool TryCalculateNearestBuildPreviewDistance(
		const FVector& SurfacePoint,
		const FVector& SurfaceNormal,
		float& OutNearestDistance) const;
	void CacheHeldBuildableActorStates(AActor* FacilityActor);
	void HideHeldBuildableActors();
	void RestoreHeldBuildableActorStates();
	bool TryCalculateBuildSnapTransform(
		const FVector& BasePlacementLocation,
		const FRotator& ManualRotation,
		FVector& OutSnappedLocation,
		FRotator& OutSnappedRotation) const;
	void RefreshBuildPlacementAreaCache();
	void CollectBuildPlacementCollisionBoxes(
		AActor* FacilityActor,
		TArray<UBoxComponent*>& OutCollisionBoxes) const;
	bool IsBuildPlacementInsideCachedAreas(
		const TArray<UBoxComponent*>& CollisionBoxes,
		const FVector& FacilityLocation,
		const FRotator& FacilityRotation) const;
	bool HasBuildPlacementCollisionOverlap(
		AActor* FacilityActor,
		const TArray<UBoxComponent*>& CollisionBoxes,
		const FVector& FacilityLocation,
		const FRotator& FacilityRotation) const;
	bool TryPickUpBuildableFacility(AActor* FacilityActor);
	void TryPlaceHeldBuildableFacility();
	void DropHeldBuildableFacilityAsUninstalled();
	bool TryCalculateBuildPlacementLocation(
		AActor* FacilityActor,
		FVector& OutPlacementLocation,
		bool& OutIsSurfaceValid) const;
	bool TryGetProductRowFromActor(
		const AActor* TargetActor,
		FDataTableRowHandle& OutProductRow) const;
	void ShowProductPrice(
		const FDataTableRowHandle& ProductRow,
		int32 RemainingQuantity = INDEX_NONE) const;
	void ShowProductShelfLife(const FCSProductRuntimeState& RuntimeState) const;
	void ShowProductFreshness(
		const FDataTableRowHandle& ProductRow,
		int32 CurrentFreshness) const;
	void ShowStorageWarning(const FDataTableRowHandle& ProductRow) const;
	void CreatePlacementPreview(ACSProductPickupActor* ProductActor);
	void CreatePlacementPreviewFromCrate(const ACSDeliveryCrateActor* DeliveryCrate);
	void DestroyPlacementPreview();
	// 상품을 들거나 내려놓는 시점에만 배치 가능한 진열대 외곽선을 갱신합니다.
	void RefreshShelfPlacementHighlights();
	// 들고 있던 상품이 없어졌을 때 모든 진열대 외곽선을 해제합니다.
	void ClearShelfPlacementHighlights();
	void SetPlacementPreviewVisible(bool bIsVisible);
	void ClearCartPlacementPreview();
	bool TryPlaceProductFromCrate(
		ACSDeliveryCrateActor* DeliveryCrate,
		AActor* ShelfActor,
		FVector PlacementHint);

	TArray<TWeakObjectPtr<UBoxComponent>> CachedBuildPlacementAreas;
	bool bBuildPlacementAreaCacheInitialized = false;


public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float WalkSpeed = 700.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float SprintSpeed = 1000.0f;

	// 웅크린 상태에서 적용할 이동 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float CrouchSpeed = 200.0f;
};
