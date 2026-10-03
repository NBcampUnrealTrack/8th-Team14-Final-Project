#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSShelfPlaceable.generated.h"

// 손님 AI와 진열 규칙에서 사용할 진열대 슬롯의 높이 구분입니다.
UENUM(BlueprintType)
enum class ECSShelfSlotLevel : uint8
{
	Lower UMETA(DisplayName = "하단"),
	Middle UMETA(DisplayName = "중단"),
	Upper UMETA(DisplayName = "상단")
};

// 진열대 프리뷰가 보여 줄 배치 결과입니다.
UENUM(BlueprintType)
enum class ECSShelfPlacementPreviewState : uint8
{
	Invalid,
	Valid,
	Warning
};

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

	// 빈 슬롯 여부와 보관 환경을 함께 판단하여 프리뷰 상태를 반환합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	ECSShelfPlacementPreviewState GetPlacementPreviewState(
		AActor* ItemActor,
		FVector PlacementHint,
		FTransform& OutSlotWorldTransform) const;

	// 선택된 빈 슬롯에 상품을 실제로 배치합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	bool TryPlaceItem(AActor* ItemActor, FVector PlacementHint);

	// 상품을 다시 집었을 때 해당 슬롯의 점유 상태를 해제합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	void RemovePlacedItem(AActor* ItemActor);

	// 지정한 상품이 놓여 있는 슬롯의 높이 정보를 반환합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Shelf")
	bool GetItemSlotLevel(AActor* ItemActor, ECSShelfSlotLevel& OutSlotLevel) const;

	// 배치할 수 있을 때 띄우는 Q 안내. 진열대가 아닌 대상은 덮어써서 바꾼다
	virtual FText GetPlacementPromptText() const
	{
		return NSLOCTEXT("Interaction", "PlacePrompt", "[Q] 진열하기");
	}
};
