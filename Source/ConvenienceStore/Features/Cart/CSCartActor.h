#pragma once

#include "CoreMinimal.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "GameFramework/Actor.h"
#include "CSCartActor.generated.h"

class UArrowComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;
class ACSDeliveryCrateActor;

// 수레 적재 칸 하나의 상대 위치와 현재 적재한 배송 상자를 보관합니다.
USTRUCT(BlueprintType)
struct FCSCartCargoSlot
{
	GENERATED_BODY()

	// 수레 액터를 기준으로 한 배송 상자의 적재 위치입니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cart")
	FTransform LocalTransform;

	// 현재 이 칸에 적재된 배송 상자입니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cart")
	TObjectPtr<AActor> OccupyingCrate = nullptr;
};

// 배송 상자만 실어 나르는 손잡이형 운반 수레입니다.
UCLASS()
class CONVENIENCESTORE_API ACSCartActor : public AActor,
	public ICSShelfPlaceable
{
	GENERATED_BODY()

public:
	ACSCartActor();
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

	// 현재 플레이어가 수레 손잡이를 잡고 있는지 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Cart|Push")
	bool IsBeingPushed() const
	{
		return bIsBeingPushed;
	}

	// 손잡이를 잡은 플레이어를 기록하고 플레이어 충돌만 일시적으로 무시합니다.
	bool BeginPushing(AActor* NewPushingActor);
	// 수레를 바닥에 내려놓고 플레이어와의 기본 충돌을 복원합니다.
	void StopPushing(AActor* RequestingActor);
	// 지정한 손잡이 기준점에 맞춰 수레 본체를 Sweep 이동합니다.
	void UpdatePushTransform(const USceneComponent* PushAnchor);
	// 조준한 빈 적재 칸에 배송 상자와 동일한 외형의 고스트 프리뷰를 표시합니다.
	void ShowCratePlacementPreview(
		const ACSDeliveryCrateActor* SourceCrate,
		const FTransform& SlotWorldTransform);
	// 수레 조준이 끝나거나 적재를 완료했을 때 고스트 프리뷰를 숨깁니다.
	void HideCratePlacementPreview();

protected:
	// 수레 메시와 슬롯 마커의 공통 부모입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 블루프린트에서 수레 모델과 충돌을 지정할 메시입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CartMesh;

	// 플레이어 손 기준점과 맞출 수레 손잡이 위치입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> HandleAnchor;

	// 배송 상자 적재 위치를 표시하는 충돌 없는 고스트 메시입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CratePlacementPreviewMesh;

	// 지정하면 모든 고스트 메시 슬롯에 적용할 반투명 프리뷰 머티리얼입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cart|Placement Preview")
	TObjectPtr<UMaterialInterface> ValidCratePreviewMaterial;

	// CartSlot_00부터 연속된 Arrow Component를 읽어 생성한 적재 칸입니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Cart")
	TArray<FCSCartCargoSlot> CargoSlots;

	// 손잡이를 잡고 있는 액터입니다.
	UPROPERTY(Transient)
	TObjectPtr<AActor> PushingActor;

	UPROPERTY(Transient)
	bool bIsBeingPushed = false;

	// 수레를 밀 때만 변경한 Pawn 충돌 응답의 원래 값을 보관합니다.
	UPROPERTY(Transient)
	TEnumAsByte<ECollisionResponse> PreviousPawnCollisionResponse = ECR_Block;

	// CartSlot_00 형식의 Arrow Component 이름에서 적재 칸 번호를 읽습니다.
	bool TryGetCargoSlotMarkerIndex(const UArrowComponent* SlotMarker, int32& OutSlotIndex) const;
	// 블루프린트의 Arrow Component 위치를 적재 칸 배열에 동기화합니다.
	void SyncCargoSlotsFromMarkers();
	// 조준 위치와 가장 가까운 빈 적재 칸을 찾습니다.
	int32 FindClosestEmptyCargoSlot(FVector PlacementHint) const;
};
