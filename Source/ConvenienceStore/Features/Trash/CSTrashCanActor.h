#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSTrashCanActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;

// 물건을 넣으면 파괴하는 쓰레기통
// 지금은 무엇이든 받아들임
// 나중에 폐기 대상을 제한하고 싶으면 CanPlaceItem_Implementation만 고치면 됨

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnItemDisposed,
	AActor*,
	DisposedItem,
	int32,
	TotalDisposedCount);

UCLASS()
class CONVENIENCESTORE_API ACSTrashCanActor : public AActor, public ICSShelfPlaceable
{
	GENERATED_BODY()

public:
	ACSTrashCanActor();

	virtual bool CanPlaceItem_Implementation(AActor* ItemActor) const override;
	virtual bool GetPlacementPreview_Implementation(
		AActor* ItemActor,
		FVector PlacementHint,
		FTransform& OutSlotWorldTransform) const override;
	virtual ECSShelfPlacementPreviewState GetPlacementPreviewState_Implementation(
		AActor* ItemActor,
		FVector PlacementHint,
		FTransform& OutSlotWorldTransform) const override;
	virtual bool TryPlaceItem_Implementation(AActor* ItemActor, FVector PlacementHint) override;
	virtual void RemovePlacedItem_Implementation(AActor* ItemActor) override;
	virtual bool GetItemSlotLevel_Implementation(
		AActor* ItemActor,
		ECSShelfSlotLevel& OutSlotLevel) const override;

	// 물건이 버려질 때마다 방송
	// 튜토리얼을 포함해 관심 있는 누구든 구독 가능
	UPROPERTY(BlueprintAssignable, Category = "Store|TrashCan")
	FCSOnItemDisposed OnItemDisposed;

	UFUNCTION(BlueprintPure, Category = "Store|TrashCan")
	int32 GetTotalDisposedCount() const
	{
		return TotalDisposedCount;
	}

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 실제 쓰레기통 모델을 끼울 자리 (나중에 BP_TrashCan에서 메시 지정)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TrashCanMesh;

private:
	int32 TotalDisposedCount = 0;
};
