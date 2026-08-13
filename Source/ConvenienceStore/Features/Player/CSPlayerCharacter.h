#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Character.h"
#include "CSPlayerCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USceneComponent;
class ACSDeliveryCrateActor;
class ACSProductPickupActor;
class ACSProductPlacementPreviewActor;
class ACSUsableToolActor;
struct FInputActionValue;
struct FCSProductRuntimeState;

UCLASS()
class CONVENIENCESTORE_API ACSPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// 플레이어 캐릭터의 기본 컴포넌트와 이동 값을 설정합니다.
	ACSPlayerCharacter();

	// 상호작용한 물체를 플레이어 손 위치에 부착합니다.
	bool TryPickUp(AActor* PickupActor);

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

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<AActor> HeldActor;

	// 들고 있는 상품의 외형을 진열대 슬롯에 표시할 프리뷰 액터 클래스입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Placement Preview")
	TSubclassOf<ACSProductPlacementPreviewActor> PlacementPreviewActorClass;

	// 현재 생성되어 있는 표시 전용 프리뷰 액터입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSProductPlacementPreviewActor> PlacementPreviewActor;
	
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

	// 좌클릭으로 현재 들고 있는 도구의 주 사용 기능을 실행합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PrimaryUseAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleClockAction;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float InteractionDistance = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float DropDistance = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float DropTraceDepth = 400.0f;

	
private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void StartCrouch();
	void StopCrouch();
	void TryInteract();
	void DropHeldItem();
	void TryUsePrimaryTool();
	void ToggleClock();
	void UpdateInteractionHUD();
	void UpdatePlacementPreview();
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
	bool TryPlaceProductFromCrate(
		ACSDeliveryCrateActor* DeliveryCrate,
		AActor* ShelfActor,
		FVector PlacementHint);
	
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float WalkSpeed = 350.0f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float SprintSpeed = 600.0f;

	// 웅크린 상태에서 적용할 이동 속도입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float CrouchSpeed = 180.0f;
};
