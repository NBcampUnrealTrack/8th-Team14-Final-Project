#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSProductAnalysisRowWidget.generated.h"

class UTextBlock;
class UWidget;

UCLASS(Abstract)
class CONVENIENCESTORE_API UCSProductAnalysisRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRowData(
		const FText& ProductName,
		int32 SoldQuantity,
		int32 RemainingStock,
		int32 DisposedQuantity);

protected:
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ProductNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SoldQuantityText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RemainingStockText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DisposedQuantityText;
	// 품절 여부에 따른 상태 마커 연출용입니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> DefaultStatusMarker;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> OutOfStockStatusBox;

	// 재고 수량 색상은 WBP Class Defaults에서 시안에 맞게 조절합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Product Analysis|Style")
	FSlateColor InStockTextColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Product Analysis|Style")
	FSlateColor OutOfStockTextColor = FSlateColor(FLinearColor(0.9f, 0.2f, 0.2f, 1.0f));
};
