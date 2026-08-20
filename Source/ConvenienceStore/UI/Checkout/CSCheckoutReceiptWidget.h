#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCheckoutTypes.h"
#include "CSCheckoutReceiptWidget.generated.h"

class UImage;
class UHorizontalBox;
class UScrollBox;
class UTextBlock;
class UCSCheckoutOrderRowWidget;

// 합계·도장·흔들림 애니메이션이 끝나 부모 계산 UI가 영수증 퇴장을 시작할 수 있음을 알립니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnReceiptCompletionFinished);

// 스캔된 상품 행과 결제 완료 결과를 표시하는 영수증 위젯의 기본 클래스입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCheckoutReceiptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 새 손님의 계산을 시작할 수 있도록 상품 행, 합계, 도장을 초기화합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout|Receipt")
	void ResetReceipt();

	// 상품 ID가 처음이면 새 행을 만들고, 이미 있으면 같은 행의 수량과 합계를 갱신합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout|Receipt")
	void AddOrUpdateLineItem(const FCSCheckoutLineItem& LineItem);

	// 결제 완료 시 영수증 하단에 최종 합계를 표시합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout|Receipt")
	void SetFinalTotal(int32 TotalSalePrice);

	// 결제 완료 도장의 표시 여부를 변경합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout|Receipt")
	void SetCompletionStampVisible(bool bVisible);

	// 최종 합계와 도장을 표시한 뒤 이 영수증 WBP의 완료 애니메이션을 요청합니다.
	void BeginReceiptCompletionSequence();

	// WBP_CSCheckoutReceipt의 완료 애니메이션 Finished에서 호출합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout|Receipt|Animation")
	void NotifyReceiptCompletionFinished();

	UPROPERTY(BlueprintAssignable, Category = "Checkout|Receipt")
	FCSOnReceiptCompletionFinished OnReceiptCompletionFinished;

protected:
	virtual void NativeConstruct() override;

	// WBP_CSCheckoutReceipt에서 합계 작성 → 도장 → 흔들림 애니메이션을 재생합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkout|Receipt|Animation")
	void OnReceiptCompletionRequested();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> ScrollBox_Receipt;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_TotalPrice;

	// 합계 문구, 룬 이미지, 합계 숫자를 묶은 영수증 하단 영역입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HorizontalBox_FinalTotal;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_CompletionStamp;

	// 영수증에 동적으로 추가할 상품 행 WBP 클래스입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Checkout|Receipt")
	TSubclassOf<UCSCheckoutOrderRowWidget> ReceiptRowWidgetClass;

private:
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UCSCheckoutOrderRowWidget>> ReceiptRowsByProductId;
};
