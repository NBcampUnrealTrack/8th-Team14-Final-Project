#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCheckoutTypes.h"
#include "CSCheckoutOrderRowWidget.generated.h"

class UTextBlock;

// 계산대 주문 목록에서 상품 한 종류를 표시하는 행 위젯입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCheckoutOrderRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 전달받은 주문 상품 정보를 행 UI에 표시합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout")
	void SetLineItem(const FCSCheckoutLineItem& InLineItem);

protected:
	// 상품명을 표시할 TextBlock입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemNameText;

	// 해당 상품의 구매 수량을 표시할 TextBlock입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> QuantityText;

	// 단가와 상품별 합계를 표시할 TextBlock입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PriceText;
};
