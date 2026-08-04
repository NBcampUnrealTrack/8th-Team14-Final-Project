#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSShelfPlaceable.generated.h"

UINTERFACE(BlueprintType)
class CONVENIENCESTORE_API UCSShelfPlaceable : public UInterface
{
	GENERATED_BODY()
};

// 상품을 슬롯에 배치하고 점유 상태를 관리하는 액터가 구현합니다.
class CONVENIENCESTORE_API ICSShelfPlaceable
{
	GENERATED_BODY()

public:
	// 현재 상품을 배치할 수 있는지 검사합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	bool CanPlaceItem(AActor* ItemActor) const;

	// 조준 위치를 기준으로 선택될 빈 슬롯의 월드 변환값을 반환합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	bool GetPlacementPreview(
		AActor* ItemActor,
		FVector PlacementHint,
		FTransform& OutSlotWorldTransform) const;

	// 선택된 빈 슬롯에 상품을 실제로 배치합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	bool TryPlaceItem(AActor* ItemActor, FVector PlacementHint);

	// 상품을 다시 집었을 때 해당 슬롯의 점유 상태를 해제합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	void RemovePlacedItem(AActor* ItemActor);
};
