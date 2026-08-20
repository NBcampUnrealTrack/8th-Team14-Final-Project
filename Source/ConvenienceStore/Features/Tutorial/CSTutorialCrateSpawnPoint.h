#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "CSTutorialCrateSpawnPoint.generated.h"

// 튜토리얼 StockShelves 스텝에서 배송 상자를 스폰할 위치와
// 그 상자에 채울 상품/수량을 지정하는 마커 액터
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSTutorialCrateSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ACSTutorialCrateSpawnPoint();

	// 마커 반짝임(HighlightMarkerTag)을 쓰고 싶을 때 DT에 그대로 입력할 기본 태그
	static const FName DefaultSpawnPointTag;

	// 여기서 스폰할 액터 클래스
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	TSubclassOf<AActor> CrateClass;

	// 스폰한 상자에 채울 상품
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	// 스폰한 상자에 채울 수량
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "1"))
	int32 Quantity = 6;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;
};
