#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSEndingData.generated.h"

class ACSEndingItemActor;
class UTexture2D;
class UUserWidget;

USTRUCT(BlueprintType)
struct FCSEndingItemDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UTexture2D> Icon = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<ACSEndingItemActor> ActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0")) int32 PriceGold = 100;
	// 이 상점 레벨에 도달하면 기존 판매 순서에 따라 구매할 수 있습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1")) int32 RequiredStoreLevel = 1;
	// 위치·회전은 슬롯 기준이며, 크기는 슬롯 배율과 무관하게 기존 아이템 크기에 곱합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform DisplayTransform = FTransform::Identity;
};

// 일반 상품 마스터와 독립적으로 귀환 재료와 연출 설정을 관리합니다.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSEndingData : public UDataAsset
{
	GENERATED_BODY()
public:
	UCSEndingData();
	// 배열 순서대로 하나씩 판매합니다. ID는 비어 있거나 중복되면 안 됩니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FCSEndingItemDefinition> Items;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0")) int32 ActivationGold = 300000;
	// 재료를 모두 제출하셔도 이 레벨에 도달하신 뒤 최종 자금을 제출하셔야 귀환합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1")) int32 ActivationStoreLevel = 10;
	// 비워 두면 코드로 만든 임시 전체 화면 위젯을 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UUserWidget> EndingWidgetClass;
	bool IsConfigurationValid() const;
};
