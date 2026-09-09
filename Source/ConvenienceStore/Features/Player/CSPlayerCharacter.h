#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Character.h"
#include "CSPlayerCharacter.generated.h"

class UCameraComponent;
class UBoxComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UAnimSequence;
class ACSCartActor;
class ACSDeliveryCrateActor;
class ACSMagicWandActor;
class ACSProductPickupActor;
class ACSProductPlacementPreviewActor;
class ACSShelfActor;
class ACSUsableToolActor;
class ACSFreshnessSafeZoneActor;
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

	// 직원 업무 시스템이 플레이어가 현재 소유한 물건을 안전하게 제외할 수 있도록 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Interaction")
	AActor* GetHeldActor() const { return HeldActor; }

	// 상호작용한 물체를 플레이어 손 위치에 부착합니다.
	bool TryPickUp(AActor* PickupActor);
	// 창고 상호작용에서 손에 든 상자 또는 낱개 상품의 소유권을 창고 슬롯으로 넘깁니다.
	bool TryStoreHeldItem(
		ACSFreshnessSafeZoneActor* StoragePoint,
		const UPrimitiveComponent* TargetInteractionComponent = nullptr);
	// 조준한 상자 슬롯에서 상자를 꺼내 플레이어 손에 부착합니다.
	bool TryTakeStoredCrate(
		ACSFreshnessSafeZoneActor* StoragePoint,
		const UPrimitiveComponent* TargetInteractionComponent);

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Locomotion")
	TObjectPtr<UAnimSequence> PlayerIdleAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Locomotion")
	TObjectPtr<UAnimSequence> PlayerWalkAnimation;

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

	// -------------------------------------------------------------------------
	// [손님 계산하기 위한 영역 시각화 변수 선언] - 백무송
	// -------------------------------------------------------------------------


    //진열대 몸통이 다른 진열대의 손님 쇼핑 판정 영역을 침범했는지 안했는지 판단하는 bool 변수
    //프리뷰의 초록/빨강 색상 전환과 HUD의 경고 문구를 별도로 띄우기 위해 사용
	UPROPERTY(Transient)
	bool bHasHeldBuildableAccessZoneConflict = false;


	// 진열대 손님 쇼핑 접근 영역이 매장 구역 안에 있는지 판단하는 bool 변수
	// 구역 밖으로 나갈 경우 배치 차단
	UPROPERTY(Transient)
	bool bIsHeldBuildableAccessZoneInsideArea = true;


	// 계산대 대기줄 슬롯 중 설 수 없는 자리(NavMesh 없음 / 다른 시설 몸통에 막힘)가 있는지 판단하는 bool 변수
	// 대기줄이 벽을 향해 뻗는 배치를 차단하기 위해 사용
	UPROPERTY(Transient)
	bool bHasHeldBuildableUnusableQueueSlots = false;


    // 현재 선택한 진열대 몸통이 어느 진열대의 손님 쇼핑 영역에 침범 했는지 찾기 위한 배열
    // AccessZoneQueryRange 범위내에 있는 진열대를 Tick마다 주기적으로 배열에 추가/삭제해서 어느 진열대 손님 쇼핑영역에 침범했는지 찾아냄
	// 빨간색으로 시각화하기 위해 그래픽 렌더링 측에 전달하는 용도로 사용됨
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UBoxComponent>> CachedBlockedAccessZoneBoxes;

	// 플레이어가 들고 있는 시설의 손님 쇼핑 판정 영역을 그려주기 위한 Mesh (추후에 계산대 손님 줄 대기 판정 영역으로 수정시 추가될수도 있어서)
	// 시설의 메인 메시 배열과 분리, 합칠 경우 Wall Snap 계산 시 바운딩 박스 크기에 평면 영역까지 덧붙여져 오작동하는 것을 방지
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> HeldAccessZonePreviewMeshes;

	// 들고 있는 진열대 중심점과 손님 쇼핑 접근 영역 박스 사이의 상대적 간격과 각도를 기억해두는 배열
	// 플레이어가 진열대를 들고 위아래를 쳐다볼 때(카메라 Pitch 변경), 바닥 영역 평면 그래픽이 같이 기울어져 땅을 파고들거나 공중에 뜨는 것을 방지
	// 매 프레임 이 값을 꺼내와 시선 기울기가 섞이지 않도록 바닥 평면을 항상 땅에 평평하게 깔아주는 위치 계산용으로 사용됨
	UPROPERTY(Transient)
	TArray<FTransform> HeldAccessZoneBoxRelativeTransforms;

	// 들고 있는 진열대의 손님 쇼핑 접근 영역 박스의 스케일이 적용되지 않은 원래 크기를 기억해두는 배열
	// HeldAccessZoneBoxRelativeTransforms와 짝을 이루어 동작함
	// 매 프레임 바닥 평면 그래픽의 스케일과 바닥과의 높이를  계산해서 맞춰줄 때 사용됨
	UPROPERTY(Transient)
	TArray<FVector> HeldAccessZoneBoxUnscaledExtents;

	// 이미 맵에 설치되어 있는 주변 진열대들의 손님 쇼핑 영역을 바닥에 그려주기 위한 평면 Mesh 재사용 풀
	// 플레이어가 들고 있는 진열대의 바닥 평면 배열과 섞이지 않도록 별개의 배열로 분리하여 관리함
	// 배열을 합칠 경우 바닥에 고정되어야 할 남의 진열대 영역들까지 플레이어 손을 따라다니는 버그가 발생하므로 이를 방지하기 위해 사용됨
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> AccessZoneDisplayMeshes;

	// 바닥에 그려진 주변 진열대들의 영역 색상을 실시간으로 바꾸기 위한 머티리얼 재사용 풀
	// 선택한 진열대가 상대방의 특정 영역을 침범했을 때, 해당 영역 그래픽만 즉각적으로 빨간색으로 바꾸기 위해 사용됨
	// 침범하지 않은 나머지 영역들은 기본 색상(청록색)으로 유지하도록 색상 파라미터를 조작하는 데 쓰임
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> AccessZoneDisplayMIDs;
	// -------------------------------------------------------------------------

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

	// false면 접근 영역 규칙이 시각화와 HUD 경고만 하고 배치는 안막음
	// 기존 레벨의 통로 폭을 정리하는 동안 임시로 내려 두는 스위치
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone")
	bool bBlockPlacementOnAccessZoneRules = true;

	// -------------------------------------------------------------------------
	// [설정값 변수 선언부 - 거리 판정 및 Z-Fighting] 백무송
	// -------------------------------------------------------------------------

	// 접근 영역 판정 대상에 포함할 최대 검색 반경
	// 이 범위 밖에 있는 멀리 떨어진 시설들은 Tick에서 제외
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone",
		meta = (ClampMin = "100.0", UIMin = "100.0"))
	float AccessZoneQueryRange = 2000.0f;

	// 플레이어 주변 바닥에 다른 진열대의 접근 영역을 렌더링할 최대 표시 거리
	// 판정 거리와 동일하게 그리면 바닥이 영역으로 뒤덮여 시야를 방해하므로, 시각화 거리만 더 짧게하기 위해 분리된 변수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone",
		meta = (ClampMin = "100.0", UIMin = "100.0"))
	float AccessZoneVisualRange = 900.0f;

	// 바닥에 그려질 접근 영역 평면의 Mesh를 할당할 변수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone")
	TObjectPtr<UStaticMesh> AccessZoneDisplayMesh;

	// 접근 영역 Mesh에 할당 할 머티리얼
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone")
	TObjectPtr<UMaterialInterface> AccessZoneDisplayMaterial;

	// 바닥 평면 그래픽을 실제 땅바닥에서 띄워주는 높이 값
	// Z-Fighting 현상 방지 (Z축에 겹치면 화면이 깜빡거리는 현상)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone")
	float AccessZoneDisplayLift = 2.0f;

	// 바닥에 표시되는 손님 쇼핑 판정영역의 기본 색상
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone")
	FLinearColor NeutralZoneColor = FLinearColor(0.1f, 0.7f, 0.9f, 0.25f);

	// 현재 들고 있는 시설이 침범 중인 영역에만 쓰는 색
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|AccessZone")
	FLinearColor BlockedZoneColor = FLinearColor(1.0f, 0.15f, 0.1f, 0.6f);

	// -------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|Rotation",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BuildRotationHoldDelay = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Build|Rotation",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float BuildRotationDegreesPerSecond = 90.0f;

	// 현재 생성되어 있는 표시 전용 프리뷰 액터입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSProductPlacementPreviewActor> PlacementPreviewActor;

	// 전체 진열대 안내 위에 단별 판정을 덮어쓰고 있는 현재 조준 진열대입니다.
	TWeakObjectPtr<ACSShelfActor> AimedShelfPlacementHighlight;
	uint8 AimedShelfPlacementPreviewState = 0;

	// 현재 배송 상자 적재 프리뷰를 표시 중인 수레입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSCartActor> CartPlacementPreviewTarget;

	// 현재 창고 상자 슬롯 프리뷰를 표시 중인 보관소입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSFreshnessSafeZoneActor> StorageCratePlacementPreviewTarget;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Throw",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ThrowHoldThreshold = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Throw",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ThrowStrength = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Throw",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float ThrowUpwardBias = 0.15f;


private:
	void UpdatePlayerLocomotionAnimation(bool bForce = false);
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void StartCrouch();
	void StopCrouch();
	void TryInteract();
	void HandleDropActionStarted();
	void HandleDropActionReleased();
	void HandleDropActionCanceled();
	void ResetDropActionHoldState();
	void DropHeldItem();
	void ThrowHeldItem();
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

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentPlayerLocomotionAnimation;

	bool bPlayerWalkingAnimation = false;

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
	// 태그로 시설의 판정용 박스를 모읍니다. 몸통과 접근 영역이 같은 함수를 씁니다.
	// [백무송] 리팩토링하여 '접근 영역' 판정 시에도 구조 변경 없이 수학 계산 코드를 재사용할 수 있게 수정
	void CollectBuildPlacementBoxesByTag(
		AActor* FacilityActor,
		const FName& BoxTag,
		TArray<UBoxComponent*>& OutBoxes) const;

	// 규칙 D. 배치 예정 위치에서 계산대 대기줄 슬롯이 하나라도 설 수 없는 자리면 배치를 거부합니다.
	// 규칙 A·B가 시설만 검사해서 벽으로 뻗는 대기줄을 잡지 못하는 구멍을 메웁니다.
	bool HasUnusableQueueSlotPlacement(
		AActor* FacilityActor,
		const FVector& FacilityLocation,
		const FRotator& FacilityRotation) const;
	// 박스는 시설 기준 상대 트랜스폼으로 풀어야 하므로 시설 액터를 함께 받습니다.
	// 계산대처럼 시설이 스폰해 소켓에 붙인 부속 액터가 태그 박스를 들고 있으면,
	// 박스 주인(부속 액터)을 기준으로 삼는 순간 시설과의 오프셋이 통째로 사라집니다.
	bool IsBuildPlacementInsideCachedAreas(
		AActor* FacilityActor,
		const TArray<UBoxComponent*>& CollisionBoxes,
		const FVector& FacilityLocation,
		const FRotator& FacilityRotation) const;
	bool HasBuildPlacementCollisionOverlap(
		AActor* FacilityActor,
		const TArray<UBoxComponent*>& CollisionBoxes,
		const FVector& FacilityLocation,
		const FRotator& FacilityRotation) const;

	// [백무송]
	// 선택한 진열대 몸통이 남의 접근 영역을 침범하는지, 내 접근 영역이 남의 진열대 몸통에 막히는지 2D SAT 방식으로 검사하는 함수
	// 마주 보는 진열대들이 공간(접근 영역)을 서로 공유할 수 있도록, 영역 대 영역 충돌 검사는 의도적으로 루프에서 배제
	// 충돌 시 OutBlockedZoneBoxes에 내가 침범한 상대방의 구체적인 영역 박스들을 담아 반환하며, 시각화 로직이 이 명단을 보고 해당 구역만 빨간색으로 칠하게 됨
	bool HasBuildPlacementAccessZoneConflict(
		AActor* FacilityActor,
		const TArray<UBoxComponent*>& MyBodyBoxes,
		const TArray<UBoxComponent*>& MyZoneBoxes,
		const FVector& FacilityLocation,
		const FRotator& FacilityRotation,
		TArray<TWeakObjectPtr<UBoxComponent>>& OutBlockedZoneBoxes) const;

	// [백무송]
	// 맵에 이미 설치되어 있는 주변 진열대들의 손님 접근 영역을 바닥 평면 그래픽으로 갱신하여 그려주는 함수
	// 플레이어가 진열대를 들고 있을 때 Tick마다 실행되며, 내가 침범한 영역과 안전한 영역을 지정한 색으로 렌더링함
	void RefreshInstalledAccessZoneDisplays();

	// [백무송]
	// 바닥에 그려진 모든 설치된 시설들의 접근 영역을 화면에서 비활성화 처리하는 함수
	// 플레이어가 진열대 배치를 완료하거나 취소해서 손을 비웠을 때, 시야를 가리는 정보를 지움
	void ClearInstalledAccessZoneDisplays();

	// [백무송]
	// 시각화에 사용할 바닥 평면 매시를 오브젝트 풀(AccessZoneDisplayMeshes)에서 가져오는 함수[cite: 3]
	// 요청한 인덱스에 해당하는 평면이 풀에 이미 존재하면 재사용하고, 부족하면 새로 생성(NewObject)하여 캐시 배열에 추가함[cite: 3]
	// 렌더링할 때마다 평면을 새로 만들고 파괴하면 성능 부하가 크므로 이를 방지하기 위해 풀링 기법을 적용함[cite: 1, 3]
	UStaticMeshComponent* AcquireAccessZoneDisplay(int32 DisplayIndex);

	// 접근 영역 박스 하나를 화면 바닥에 그릴 때 사용할 위치, 회전, 스케일을 계산하는 함수
	// 크기나 피벗이 다른 매시로 교체해도 그려지는 판정 박스와 일치함
	// 실패하면 false를 돌려주어 그리기를 건너뜀
	bool TryCalculateAccessZoneDisplayTransform(
		const FVector& ZoneCenter,
		const FVector& ZoneScaledExtent,
		float ZoneYaw,
		FVector& OutLocation,
		FRotator& OutRotation,
		FVector& OutScale) const;

	bool TryPickUpBuildableFacility(AActor* FacilityActor);
	void TryPlaceHeldBuildableFacility();
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
	void ShowEmployeeReservationWarning(const FText& Message) const;
	void CreatePlacementPreview(ACSProductPickupActor* ProductActor);
	void CreatePlacementPreviewFromCrate(const ACSDeliveryCrateActor* DeliveryCrate);
	void DestroyPlacementPreview();
	// 상품을 들거나 내려놓는 시점에만 배치 가능한 진열대 외곽선을 갱신합니다.
	void RefreshShelfPlacementHighlights();
	// 조준 진열대만 단별 상태로 덮어쓰며, 나머지는 전체 빈칸 기준 안내를 유지합니다.
	void UpdateAimedShelfPlacementHighlight(
		ACSShelfActor* ShelfActor,
		uint8 PreviewStateValue);
	// 진열대 조준을 벗어나면 단별 덮어쓰기를 해제하고 전체 안내를 복구합니다.
	void RestoreGlobalShelfPlacementHighlights();
	// 들고 있던 상품이 없어졌을 때 모든 진열대 외곽선을 해제합니다.
	void ClearShelfPlacementHighlights();
	void SetPlacementPreviewVisible(bool bIsVisible);
	void ClearCartPlacementPreview();
	void ClearStorageCratePlacementPreview();
	bool TryPlaceProductFromCrate(
		ACSDeliveryCrateActor* DeliveryCrate,
		AActor* ShelfActor,
		FVector PlacementHint);

	TArray<TWeakObjectPtr<UBoxComponent>> CachedBuildPlacementAreas;

	// [백무송]
	// 접근 영역 충돌 판정의 대상이 되는 맵 상의 모든 설치된 시설(진열대 등) 액터들의 목록을 미리 모아둔 배열
	// 건축 모드 진입 시 Area 캐시를 돌 때 한 번만 순회하여 이 배열에 담아두고 판정에 재사용함
	TArray<TWeakObjectPtr<AActor>> CachedAccessZoneFacilities;
	bool bBuildPlacementAreaCacheInitialized = false;
	bool bIsDropActionHeld = false;
	float DropActionStartedTimeSeconds = 0.0f;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> DropActionHeldActor;


public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float WalkSpeed = 700.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float SprintSpeed = 1000.0f;

	// 웅크린 상태에서 적용할 이동 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float CrouchSpeed = 200.0f;
};
