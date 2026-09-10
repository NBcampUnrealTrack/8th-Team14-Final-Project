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
	// 비워 두면 코드로 만든 임시 전체 화면 위젯을 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UUserWidget> EndingWidgetClass;
	bool IsConfigurationValid() const;
};
