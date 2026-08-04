#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSShelfActor.generated.h"

class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct FCSShelfSlot
{
	GENERATED_BODY()

	// 진열대 액터를 기준으로 한 상품의 위치와 회전입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf")
	FTransform LocalTransform;

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

	virtual bool CanPlaceItem_Implementation(AActor* ItemActor) const override;
	virtual bool GetPlacementPreview_Implementation(
		AActor* ItemActor,
		FVector PlacementHint,
		FTransform& OutSlotWorldTransform) const override;
	virtual bool TryPlaceItem_Implementation(
		AActor* ItemActor,
		FVector PlacementHint) override;
	virtual void RemovePlacedItem_Implementation(AActor* ItemActor) override;

protected:
	int32 FindClosestEmptySlot(FVector PlacementHint) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShelfMesh;

	// 진열대 블루프린트에서 상품 위치마다 하나씩 추가합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shelf")
	TArray<FCSShelfSlot> Slots;
};
