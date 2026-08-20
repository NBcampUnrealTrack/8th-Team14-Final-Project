#include "UI/Checkout/CSCheckoutReceiptWidget.h"

#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "UI/Checkout/CSCheckoutOrderRowWidget.h"

void UCSCheckoutReceiptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResetReceipt();
}

void UCSCheckoutReceiptWidget::ResetReceipt()
{
	ReceiptRowsByProductId.Reset();

	if (IsValid(ScrollBox_Receipt))
	{
		ScrollBox_Receipt->ClearChildren();
	}

	if (IsValid(Text_TotalPrice))
	{
		Text_TotalPrice->SetText(FText::GetEmpty());
	}

	if (IsValid(HorizontalBox_FinalTotal))
	{
		HorizontalBox_FinalTotal->SetVisibility(ESlateVisibility::Collapsed);
	}

	SetCompletionStampVisible(false);
}

void UCSCheckoutReceiptWidget::AddOrUpdateLineItem(
	const FCSCheckoutLineItem& LineItem)
{
	if (LineItem.ProductId.IsNone())
	{
		return;
	}

	if (TObjectPtr<UCSCheckoutOrderRowWidget>* ExistingRow =
		ReceiptRowsByProductId.Find(LineItem.ProductId))
	{
		if (IsValid(ExistingRow->Get()))
		{
			ExistingRow->Get()->SetLineItem(LineItem);
		}
		return;
	}

	if (!IsValid(ScrollBox_Receipt) || ReceiptRowWidgetClass == nullptr)
	{
		return;
	}

	UCSCheckoutOrderRowWidget* NewRow = CreateWidget<UCSCheckoutOrderRowWidget>(
		this,
		ReceiptRowWidgetClass);
	if (!IsValid(NewRow))
	{
		return;
	}

	// 먼저 ScrollBox에 붙여 실제 레이아웃을 만든 뒤 데이터를 설정해야 Row 작성 애니메이션의 첫 프레임이 보입니다.
	ScrollBox_Receipt->AddChild(NewRow);
	ReceiptRowsByProductId.Add(LineItem.ProductId, NewRow);
	NewRow->SetLineItem(LineItem);
}

void UCSCheckoutReceiptWidget::SetFinalTotal(int32 TotalSalePrice)
{
	if (!IsValid(Text_TotalPrice))
	{
		return;
	}

	Text_TotalPrice->SetText(FText::Format(
		NSLOCTEXT("Checkout", "ReceiptFinalTotal", "{0} G"),
		FText::AsNumber(FMath::Max(0, TotalSalePrice))));

	if (IsValid(HorizontalBox_FinalTotal))
	{
		HorizontalBox_FinalTotal->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UCSCheckoutReceiptWidget::SetCompletionStampVisible(bool bVisible)
{
	if (IsValid(Image_CompletionStamp))
	{
		Image_CompletionStamp->SetVisibility(
			bVisible
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}
}

void UCSCheckoutReceiptWidget::BeginReceiptCompletionSequence()
{
	// Collapsed 상태에서는 애니메이션이 그려지지 않으므로 재생 전에 도장을 레이아웃에 복귀시킵니다.
	SetCompletionStampVisible(true);
	OnReceiptCompletionRequested();
}

void UCSCheckoutReceiptWidget::NotifyReceiptCompletionFinished()
{
	OnReceiptCompletionFinished.Broadcast();
}
