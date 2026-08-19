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
	// 처음 생성된 상품 행의 데이터가 모두 채워진 뒤 WBP에 전체 작성 연출을 요청합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkout|Receipt Row")
	void OnInitialLineItemDisplayed();

	// 같은 상품을 다시 스캔하여 수량과 행 합계가 바뀐 경우 WBP에 갱신 연출을 요청합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkout|Receipt Row")
	void OnLineItemQuantityUpdated();

	// 상품명을 표시할 TextBlock입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ProductName;

	// 해당 상품의 구매 수량을 표시할 TextBlock입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Quantity;

	// 상품 한 개의 판매 단가를 표시할 TextBlock입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_UnitPrice;

	// 수량을 반영한 상품별 합계를 표시할 TextBlock입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_LineTotal;

private:
	// 첫 데이터 설정과 같은 상품의 후속 갱신을 구분하는 표시 전용 상태입니다.
	bool bHasDisplayedLineItem = false;
};
