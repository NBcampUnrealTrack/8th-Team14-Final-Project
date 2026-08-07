#include "UI/Checkout/CSCheckoutWidget.h"

#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "UI/Checkout/CSCheckoutOrderRowWidget.h"

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
	LastScannedItemName = FText::GetEmpty();

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
	LastScannedItemName = LineItem.ItemName;
	++ScannedItemCount;
	++ScannedQuantityInCurrentLine;
	ScannedSalePrice += LineItem.UnitSalePrice;

	// 계산대가 같은 상품의 표시용 액터를 제거하거나 연출할 수 있도록 스캔 대상을 알립니다.
	OnItemScanned.Broadcast(LineItem.ProductId);
	UpdateCheckoutDisplay();
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
	if (!IsScanComplete())
	{
		return;
	}

	// 실제 손님 퇴장과 대기열 갱신은 계산대가 담당하므로 이벤트로만 요청합니다.
	OnPaymentRequested.Broadcast();
}

void UCSCheckoutWidget::HandleCancelCheckoutClicked()
{
	// 계산 취소는 손님의 장바구니와 대기열을 변경하지 않고 UI를 닫는 용도로만 사용합니다.
	OnCheckoutCancelled.Broadcast();
}

void UCSCheckoutWidget::UpdateCheckoutDisplay()
{
	if (IsValid(ScanProgressText))
	{
		ScanProgressText->SetText(FText::Format(
			NSLOCTEXT("Checkout", "ScanProgress", "스캔: {0} / {1}"),
			FText::AsNumber(ScannedItemCount),
			FText::AsNumber(TotalItemCount)));
	}

	if (IsValid(ScannedTotalText))
	{
		ScannedTotalText->SetText(FText::Format(
			NSLOCTEXT("Checkout", "ScannedTotal", "합계: {0} 골드"),
			FText::AsNumber(ScannedSalePrice)));
	}

	if (IsValid(LastScannedItemText))
	{
		LastScannedItemText->SetText(LastScannedItemName);
	}

	if (IsValid(CheckoutStatusText))
	{
		CheckoutStatusText->SetText(IsScanComplete()
			? NSLOCTEXT("Checkout", "ReadyToPay", "스캔이 완료되었습니다. 결제를 진행해 주세요.")
			: NSLOCTEXT("Checkout", "Scanning", "상품을 스캔해 주세요."));
	}

	if (IsValid(ScanNextItemButton))
	{
		ScanNextItemButton->SetIsEnabled(!IsScanComplete());
	}

	if (IsValid(CompleteCheckoutButton))
	{
		// 결제 완료 버튼의 실제 처리 연결은 계산대 결제 단계에서 추가합니다.
		CompleteCheckoutButton->SetIsEnabled(IsScanComplete());
	}

	// C++에서 주문 목록 행을 갱신하고, 기존 블루프린트 표시 확장 이벤트도 계속 호출합니다.
	RefreshOrderList();
	RefreshCheckoutDisplay();
}

void UCSCheckoutWidget::RefreshOrderList()
{
	// ScrollBox 또는 행 위젯 클래스가 아직 설정되지 않은 경우에는 기존 UI 기능만 유지합니다.
	if (!IsValid(OrderListScrollBox) || OrderRowWidgetClass == nullptr)
	{
		return;
	}

	// 주문이 바뀌거나 스캔할 때마다 이전 행을 제거하여 중복 표시를 방지합니다.
	OrderListScrollBox->ClearChildren();

	for (const FCSCheckoutLineItem& LineItem : OrderItems)
	{
		// 유효하지 않은 수량의 데이터는 주문 목록에 표시하지 않습니다.
		if (LineItem.Quantity <= 0)
		{
			continue;
		}

		UCSCheckoutOrderRowWidget* OrderRowWidget = CreateWidget<UCSCheckoutOrderRowWidget>(
			this,
			OrderRowWidgetClass);
		if (!IsValid(OrderRowWidget))
		{
			continue;
		}

		// 생성한 행에 상품 데이터를 전달한 뒤 ScrollBox의 마지막 자식으로 추가합니다.
		OrderRowWidget->SetLineItem(LineItem);
		OrderListScrollBox->AddChild(OrderRowWidget);
	}
}
