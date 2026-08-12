#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSCustomerBasketActor.generated.h"

class UArrowComponent;
class USceneComponent;
class UStaticMeshComponent;
class ACSProductPickupActor;
struct FCSSelectProduct;

// 고객이 선택한 상품을 시각적으로 담아 보여 주는 장바구니 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSCustomerBasketActor : public AActor
{
	GENERATED_BODY()

public:
	ACSCustomerBasketActor();

	// 구매 목록에 추가된 상품을 장바구니의 다음 빈 표시 슬롯에 생성합니다.
	bool AddDisplayProduct(const FCSSelectProduct& SelectedProduct);
	// 계산, 반품, 이탈 시 표시용 상품을 모두 제거합니다.
	void ClearDisplayProducts();
	// 계산 중에는 장바구니와 표시 상품을 함께 숨기거나 다시 보여 줍니다.
	void SetBasketDisplayVisible(bool bShouldBeVisible);

protected:
	// BasketSlot_00, BasketSlot_01 형식의 Arrow Component를 표시 위치로 사용합니다.
	void CollectDisplaySlots();
	bool TryGetDisplaySlotIndex(const UArrowComponent* SlotMarker, int32& OutSlotIndex) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BasketMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UArrowComponent>> DisplaySlots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACSProductPickupActor>> DisplayProducts;
};
