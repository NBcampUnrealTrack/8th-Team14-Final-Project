#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSCraftStationInfoWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

// 솥 위 정보 창. 표시할 값은 제조대가 계산해서 넣어 준다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftStationInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// SlotIndex 0은 재료 A, 1은 재료 B. Count가 0이면 칸을 비운다
	void SetIngredient(int32 SlotIndex, UTexture2D* Icon, int32 Count);
	void SetResultName(const FText& ResultName);
	void SetStatus(const FText& Status);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IngredientIconA;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IngredientIconB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> IngredientCountA;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> IngredientCountB;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ResultNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;
};
