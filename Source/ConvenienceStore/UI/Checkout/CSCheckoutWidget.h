#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCheckoutTypes.h"
#include "CSCheckoutWidget.generated.h"

class UButton;
class UAudioComponent;
class UScrollBox;
class UTextBlock;
class UCSCheckoutOrderRowWidget;

// 모든 상품 스캔 후 결제를 요청할 때 계산대에 알리는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnCheckoutPaymentRequested);

// 계산을 취소하고 손님을 대기열에 유지할 때 계산대에 알리는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnCheckoutCancelled);

// 상품 한 개의 스캔이 끝났을 때 계산대의 표시용 상품을 갱신하도록 알리는 이벤트입니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnCheckoutItemScanned, FName, ProductId);

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

	// 데이터 스캔뿐 아니라 마지막 바코드음 재생까지 끝난 실제 결제 가능 상태입니다.
	UFUNCTION(BlueprintPure, Category = "Checkout")
	bool CanCompleteCheckout() const;

	// 블루프린트에서 주문 목록을 생성하거나 갱신할 수 있도록 호출합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkout")
	void RefreshCheckoutDisplay();

	// 블루프린트 UI에서 상품별 행을 만들 때 사용할 주문 목록입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	TArray<FCSCheckoutLineItem> OrderItems;

	// 가장 최근에 스캔한 상품 이름입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	FText LastScannedItemName;

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

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 상품을 한 줄씩 스캔할 수 있는 버튼입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ScanNextItemButton;

	// 모든 스캔이 끝난 뒤 사용할 결제 완료 버튼입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CompleteCheckoutButton;

	// 주문을 취소하고 계산 UI를 닫는 버튼입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelCheckoutButton;

	// 현재 스캔 진행도를 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScanProgressText;

	// 스캔된 상품의 합계를 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScannedTotalText;

	// 가장 최근에 스캔한 상품 이름을 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LastScannedItemText;

	// 계산 진행 상태와 안내 문구를 표시할 텍스트입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CheckoutStatusText;

	// 주문 상품 행을 동적으로 추가할 ScrollBox입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> OrderListScrollBox;

	// 주문 목록에 추가할 행 위젯 클래스입니다. 블루프린트 자식 클래스를 지정하면됩니다.
	UPROPERTY(EditDefaultsOnly, Category = "Checkout|UI")
	TSubclassOf<UCSCheckoutOrderRowWidget> OrderRowWidgetClass;

	// 스캔 버튼 클릭을 다음 상품 스캔 처리로 연결합니다.
	UFUNCTION()
	void HandleScanNextItemClicked();

	// 모든 상품을 스캔한 경우에만 계산대에 결제 완료를 요청합니다.
	UFUNCTION()
	void HandleCompleteCheckoutClicked();

	// 거래와 대기열을 변경하지 않고 계산 취소를 계산대에 요청합니다.
	UFUNCTION()
	void HandleCancelCheckoutClicked();

	UFUNCTION()
	void HandleBarcodeScanAudioFinished();

	// 현재 주문 목록을 기준으로 ScrollBox의 행을 새로 생성합니다.
	void RefreshOrderList();

private:
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
