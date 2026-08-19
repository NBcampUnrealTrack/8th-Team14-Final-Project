#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCheckoutTypes.h"
#include "CSCheckoutWidget.generated.h"

class UButton;
class UAudioComponent;
class UTextBlock;
class UCSCheckoutReceiptWidget;

// 모든 상품 스캔 후 결제를 요청할 때 계산대에 알리는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnCheckoutPaymentRequested);

// 계산을 취소하고 손님을 대기열에 유지할 때 계산대에 알리는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnCheckoutCancelled);

// 상품 한 개의 스캔이 끝났을 때 계산대의 표시용 상품을 갱신하도록 알리는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnCheckoutItemScanned, FName, ProductId);

// 영수증 완료·퇴장 연출이 모두 끝나 다음 손님 또는 UI 종료를 준비할 수 있음을 계산대에 알립니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnReceiptExitFinished);

// 손님 주문을 표시하고 상품별 스캔 진행 상태를 관리하는 계산대 UI의 기본 클래스입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCheckoutWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 계산대에서 생성한 주문 목록과 총판매가를 위젯의 초기 데이터로 설정합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout")
	void SetCheckoutOrder(
		const TArray<FCSCheckoutLineItem>& InOrderItems,
		int32 InTotalSalePrice);

	// 다음 상품 한 개를 스캔하고, 스캔에 성공했는지 반환합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout")
	bool ScanNextItem();

	// 모든 상품을 스캔하여 결제할 수 있는 상태인지 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Checkout")
	bool IsScanComplete() const;

	// 결제가 확정된 뒤 입력을 잠그고 최종 합계·도장·영수증 퇴장 연출을 WBP에 요청합니다.
	void BeginCheckoutCompletionSequence();

	// 다음 손님의 데이터가 적용된 뒤 새 영수증 등장 연출을 WBP에 요청합니다.
	void BeginNextCustomerReceipt();

	// WBP의 영수증 퇴장 애니메이션 Finished에서 호출합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout|Animation")
	void NotifyReceiptExitFinished();

	// 데이터 스캔뿐 아니라 마지막 바코드음 재생까지 끝난 실제 결제 가능 상태입니다.
	UFUNCTION(BlueprintPure, Category = "Checkout")
	bool CanCompleteCheckout() const;

	// 블루프린트 UI에서 상품별 행을 만들 때 사용할 주문 목록입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	TArray<FCSCheckoutLineItem> OrderItems;

	// 현재까지 스캔한 상품 개수입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 ScannedItemCount = 0;

	// 주문에 포함된 전체 상품 개수입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 TotalItemCount = 0;

	// 현재까지 스캔한 상품의 판매가 합계입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 ScannedSalePrice = 0;

	// 모든 상품을 스캔했을 때의 최종 판매가입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 TotalSalePrice = 0;

	// 결제 완료 버튼을 눌렀을 때 계산대가 실제 결제를 처리하도록 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Checkout")
	FCSOnCheckoutPaymentRequested OnPaymentRequested;

	// 취소 버튼을 눌렀을 때 계산대가 UI를 닫도록 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Checkout")
	FCSOnCheckoutCancelled OnCheckoutCancelled;

	// 상품 한 개를 성공적으로 스캔했을 때 해당 상품의 RowName을 전달합니다.
	UPROPERTY(BlueprintAssignable, Category = "Checkout")
	FCSOnCheckoutItemScanned OnItemScanned;

	// 영수증 퇴장 완료 후 계산대가 다음 대기 손님을 선택하도록 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Checkout")
	FCSOnReceiptExitFinished OnReceiptExitFinished;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 상품 스캔 결과가 화면에 반영된 뒤 WBP에 숫자와 합계 피드백 연출을 요청합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkout|Animation")
	void OnScanFeedbackRequested();

	// 자식 영수증의 완료 연출이 끝난 뒤 WBP에서 영수증 퇴장 애니메이션을 재생합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkout|Animation")
	void OnReceiptExitRequested();

	// 다음 주문 초기화가 끝난 뒤 WBP에서 새 영수증 등장 애니메이션을 재생합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkout|Animation")
	void OnNextCustomerReceiptRequested();

	// 상품을 한 줄씩 스캔할 수 있는 버튼입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ScanNextItemButton;

	// 모든 스캔이 끝난 뒤 사용할 결제 완료 버튼입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CompleteCheckoutButton;

	// 주문을 취소하고 계산 UI를 닫는 버튼입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelCheckoutButton;

	// 현재까지 스캔한 상품 개수를 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ScannedCount;

	// 주문에 포함된 전체 상품 개수를 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_TotalItemCount;

	// 스캔된 상품의 합계를 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScannedTotalText;

	// 계산 진행 상태와 안내 문구를 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CheckoutStatusText;

	// 새 계산대 화면에 배치된 영수증 자식 위젯입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout|Receipt",
		meta = (BindWidgetOptional))
	TObjectPtr<UCSCheckoutReceiptWidget> ReceiptWidget;

	// 스캔 버튼 클릭을 다음 상품 스캔 처리로 연결합니다.
	UFUNCTION()
	void HandleScanNextItemClicked();

	// 모든 상품을 스캔한 경우에만 계산대에 결제 완료를 요청합니다.
	UFUNCTION()
	void HandleCompleteCheckoutClicked();

	// 거래와 대기열을 변경하지 않고 계산 취소를 계산대에 요청합니다.
	UFUNCTION()
	void HandleCancelCheckoutClicked();

	// 자식 영수증의 합계·도장·흔들림 연출 완료를 받아 부모 영수증 퇴장을 요청합니다.
	UFUNCTION()
	void HandleReceiptCompletionFinished();

	UFUNCTION()
	void HandleBarcodeScanAudioFinished();

private:
	// 완료 연출 중 스캔·결제·취소가 중복 실행되지 않게 막습니다.
	bool bIsCheckoutTransitioning = false;

	// 바코드음이 끝날 때까지만 다음 스캔과 결제를 막는 계산대 전용 상태입니다.
	UPROPERTY(Transient)
	bool bBarcodeScanAudioLocked = false;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveBarcodeScanAudioComponent;

	// 현재 스캔할 주문 행의 인덱스입니다.
	int32 CurrentLineItemIndex = 0;

	// 현재 주문 행에서 스캔한 수량입니다.
	int32 ScannedQuantityInCurrentLine = 0;

	// 숫자와 버튼 상태, 블루프린트 주문 목록 표시를 한 번에 갱신합니다.
	void UpdateCheckoutDisplay();
	bool StartBarcodeScanAudio();
	void ClearBarcodeScanAudioState();
};
