#include "UI/Checkout/CSCheckoutWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UI/Checkout/CSCheckoutReceiptWidget.h"

void UCSCheckoutWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 스캔 버튼이 블루프린트에 배치된 경우에만 클릭 이벤트를 연결합니다.
	if (IsValid(ScanNextItemButton))
	{
		ScanNextItemButton->OnClicked.AddDynamic(this, &UCSCheckoutWidget::HandleScanNextItemClicked);
	}

	// 결제 완료 버튼이 블루프린트에 배치된 경우에만 클릭 이벤트를 연결합니다.
	if (IsValid(CompleteCheckoutButton))
	{
		CompleteCheckoutButton->OnClicked.AddDynamic(
			this, &UCSCheckoutWidget::HandleCompleteCheckoutClicked);
	}

	// 취소 버튼이 블루프린트에 배치된 경우에만 클릭 이벤트를 연결합니다.
	if (IsValid(CancelCheckoutButton))
	{
		CancelCheckoutButton->OnClicked.AddDynamic(
			this, &UCSCheckoutWidget::HandleCancelCheckoutClicked);
	}

	// 자식 영수증 애니메이션의 실제 Finished 신호로 부모 퇴장 연출을 이어갑니다.
	if (IsValid(ReceiptWidget))
	{
		ReceiptWidget->OnReceiptCompletionFinished.AddUniqueDynamic(
			this, &UCSCheckoutWidget::HandleReceiptCompletionFinished);
	}

	UpdateCheckoutDisplay();
}

void UCSCheckoutWidget::SetCheckoutOrder(
	const TArray<FCSCheckoutLineItem>& InOrderItems,
	int32 InTotalSalePrice)
{
	// 이전 손님의 스캔 진행 상태를 초기화하여 주문이 섞이지 않게 합니다.
	OrderItems = InOrderItems;
	TotalSalePrice = FMath::Max(0, InTotalSalePrice);
	ScannedItemCount = 0;
	ScannedSalePrice = 0;
	CurrentLineItemIndex = 0;
	ScannedQuantityInCurrentLine = 0;
	bIsCheckoutTransitioning = false;

	if (IsValid(ReceiptWidget))
	{
		ReceiptWidget->ResetReceipt();
	}

	TotalItemCount = 0;
	for (const FCSCheckoutLineItem& LineItem : OrderItems)
	{
		// 잘못된 음수 수량은 UI 진행도에 반영하지 않습니다.
		TotalItemCount += FMath::Max(0, LineItem.Quantity);
	}

	UpdateCheckoutDisplay();
}

bool UCSCheckoutWidget::ScanNextItem()
{
	// 이전 손님의 결제 완료 연출 중에는 다음 입력을 받지 않습니다.
	if (bIsCheckoutTransitioning)
	{
		return false;
	}

	// 모든 주문 행을 스캔한 경우에는 추가 스캔을 처리하지 않습니다.
	if (IsScanComplete())
	{
		return false;
	}

	// 수량이 0인 주문 행은 건너뛰고 실제로 스캔할 상품 행을 찾습니다.
	while (OrderItems.IsValidIndex(CurrentLineItemIndex) &&
		ScannedQuantityInCurrentLine >= OrderItems[CurrentLineItemIndex].Quantity)
	{
		++CurrentLineItemIndex;
		ScannedQuantityInCurrentLine = 0;
	}

	if (!OrderItems.IsValidIndex(CurrentLineItemIndex))
	{
		return false;
	}

	const FCSCheckoutLineItem& LineItem = OrderItems[CurrentLineItemIndex];
	++ScannedItemCount;
	++ScannedQuantityInCurrentLine;
	ScannedSalePrice += LineItem.UnitSalePrice;

	if (IsValid(ReceiptWidget))
	{
		// 기존 스캔 진행 상태를 표시용 데이터로 복사해 영수증에는 스캔 완료분만 전달합니다.
		FCSCheckoutLineItem ReceiptLineItem = LineItem;
		ReceiptLineItem.Quantity = ScannedQuantityInCurrentLine;
		ReceiptLineItem.LineTotalSalePrice =
			ReceiptLineItem.UnitSalePrice * ReceiptLineItem.Quantity;
		ReceiptWidget->AddOrUpdateLineItem(ReceiptLineItem);
	}

	// 계산대가 같은 상품의 표시용 액터를 제거하거나 연출할 수 있도록 스캔 대상을 알립니다.
	OnItemScanned.Broadcast(LineItem.ProductId);

	// 숫자와 현재 합계를 먼저 새 값으로 갱신한 뒤 WBP가 해당 값에 피드백 연출을 적용하게 합니다.
	UpdateCheckoutDisplay();
	OnScanFeedbackRequested();
	return true;
}

bool UCSCheckoutWidget::IsScanComplete() const
{
	// 비어 있는 주문은 결제 가능한 주문으로 취급하지 않습니다.
	return TotalItemCount > 0 && ScannedItemCount >= TotalItemCount;
}

void UCSCheckoutWidget::HandleScanNextItemClicked()
{
	// 버튼 클릭은 다음 상품 한 개를 스캔하는 동작으로 처리합니다.
	ScanNextItem();
}

void UCSCheckoutWidget::HandleCompleteCheckoutClicked()
{
	// 스캔이 끝나지 않은 주문은 결제 요청을 보내지 않습니다.
	if (bIsCheckoutTransitioning || !IsScanComplete())
	{
		return;
	}

	// 합계 표시는 거래 검증이 성공한 뒤 계산대가 완료 연출을 요청할 때 시작합니다.
	OnPaymentRequested.Broadcast();
}

void UCSCheckoutWidget::HandleCancelCheckoutClicked()
{
	if (bIsCheckoutTransitioning)
	{
		return;
	}

	// 계산 취소는 손님의 장바구니와 대기열을 변경하지 않고 UI를 닫는 용도로만 사용합니다.
	OnCheckoutCancelled.Broadcast();
}

void UCSCheckoutWidget::BeginCheckoutCompletionSequence()
{
	if (bIsCheckoutTransitioning)
	{
		return;
	}

	// 결제가 확정된 순간부터 퇴장 완료까지 모든 버튼 입력을 잠급니다.
	bIsCheckoutTransitioning = true;
	if (IsValid(ReceiptWidget))
	{
		ReceiptWidget->SetFinalTotal(TotalSalePrice);
		ReceiptWidget->BeginReceiptCompletionSequence();
	}
	UpdateCheckoutDisplay();
}

void UCSCheckoutWidget::BeginNextCustomerReceipt()
{
	// SetCheckoutOrder로 새 데이터와 영수증이 초기화된 뒤 등장 연출만 별도로 요청합니다.
	OnNextCustomerReceiptRequested();
}

void UCSCheckoutWidget::NotifyReceiptExitFinished()
{
	// 완료 연출 중 한 번만 계산대에 전달하여 애니메이션 Finished의 중복 연결을 방어합니다.
	if (!bIsCheckoutTransitioning)
	{
		return;
	}

	OnReceiptExitFinished.Broadcast();
}

void UCSCheckoutWidget::HandleReceiptCompletionFinished()
{
	if (bIsCheckoutTransitioning)
	{
		OnReceiptExitRequested();
	}
}

void UCSCheckoutWidget::UpdateCheckoutDisplay()
{
	if (IsValid(Text_ScannedCount))
	{
		Text_ScannedCount->SetText(FText::AsNumber(ScannedItemCount));
	}

	if (IsValid(Text_TotalItemCount))
	{
		Text_TotalItemCount->SetText(FText::AsNumber(TotalItemCount));
	}

	if (IsValid(ScannedTotalText))
	{
		ScannedTotalText->SetText(FText::AsNumber(ScannedSalePrice));
	}

	if (IsValid(CheckoutStatusText))
	{
		CheckoutStatusText->SetText(IsScanComplete()
			? NSLOCTEXT("Checkout", "ReadyToPay", "결제를 완료해 주세요.")
			: NSLOCTEXT("Checkout", "Scanning", "상품을 스캔해 주세요."));
	}

	if (IsValid(ScanNextItemButton))
	{
		ScanNextItemButton->SetIsEnabled(!bIsCheckoutTransitioning && !IsScanComplete());
	}

	if (IsValid(CompleteCheckoutButton))
	{
		// 결제 완료 버튼의 실제 처리 연결은 계산대 결제 단계에서 추가합니다.
		CompleteCheckoutButton->SetIsEnabled(!bIsCheckoutTransitioning && IsScanComplete());
	}

	if (IsValid(CancelCheckoutButton))
	{
		CancelCheckoutButton->SetIsEnabled(!bIsCheckoutTransitioning);
	}

}
