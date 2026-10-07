#include "UI/Store/CSLedgerProductRowWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"

namespace
{
FText GetProductRowCategoryText(ECSProductCategory Category)
{
	switch (Category)
	{
	case ECSProductCategory::Alcohol: return FText::FromString(TEXT("주류"));
	case ECSProductCategory::Beverage: return FText::FromString(TEXT("음료"));
	case ECSProductCategory::FreshFood: return FText::FromString(TEXT("신선식품"));
	case ECSProductCategory::FrozenFood: return FText::FromString(TEXT("냉동식품"));
	case ECSProductCategory::Potion: return FText::FromString(TEXT("포션"));
	case ECSProductCategory::Scroll: return FText::FromString(TEXT("두루마리"));
	case ECSProductCategory::Snack: return FText::FromString(TEXT("간식"));
	case ECSProductCategory::Supply: return FText::FromString(TEXT("보급품"));
	case ECSProductCategory::Toiletry: return FText::FromString(TEXT("위생용품"));
	case ECSProductCategory::Tool: return FText::FromString(TEXT("도구"));
	case ECSProductCategory::HangoverCure: return FText::FromString(TEXT("숙취해소제"));
	default: return FText::GetEmpty();
	}
}
}

void UCSLedgerProductRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(ProductButton))
	{
		ProductButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleProductButtonClicked);
	}
}

void UCSLedgerProductRowWidget::NativeDestruct()
{
	if (IsValid(ProductButton))
	{
		ProductButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleProductButtonClicked);
	}
	Super::NativeDestruct();
}

void UCSLedgerProductRowWidget::SetProductData(
	const FCSLedgerProductViewData& InProductData,
	bool bInSelected)
{
	DisplayedProduct = InProductData;
	bSelected = bInSelected;
	RefreshRowView();
}

void UCSLedgerProductRowWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	RefreshRowView();
}

void UCSLedgerProductRowWidget::RefreshRowView()
{
	if (IsValid(Txt_ProductName))
	{
		Txt_ProductName->SetText(DisplayedProduct.ProductName);
		Txt_ProductName->SetColorAndOpacity(bSelected ? SelectedNameColor : DefaultNameColor);
	}
	if (IsValid(Txt_Category)) Txt_Category->SetText(GetProductRowCategoryText(DisplayedProduct.Category));
	if (IsValid(Txt_Stock))
	{
		Txt_Stock->SetText(FText::Format(
			NSLOCTEXT("StoreLedger", "ProductRowStock", "{0}개"),
			FText::AsNumber(DisplayedProduct.CurrentStock)));
		Txt_Stock->SetColorAndOpacity(bSelected ? SelectedStockColor : DefaultStockColor);
		Txt_Stock->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (IsValid(Img_Product))
	{
		Img_Product->SetBrushFromTexture(DisplayedProduct.ProductIcon, true);
		Img_Product->SetVisibility(IsValid(DisplayedProduct.ProductIcon)
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Hidden);
	}

	// 일반 상품은 항상 열린 상태이며, 제조 전용 미발견 상품에만 기존 장부의
	// 자물쇠 연출을 재사용합니다. 클릭은 유지하여 오른쪽 제조 힌트를 볼 수 있습니다.
	const ESlateVisibility LockVisibility = DisplayedProduct.bUnlocked
		? ESlateVisibility::Collapsed
		: ESlateVisibility::HitTestInvisible;
	if (IsValid(LockOnImage)) LockOnImage->SetVisibility(LockVisibility);
	if (IsValid(LockedStatus)) LockedStatus->SetVisibility(LockVisibility);
	if (IsValid(SelBg_Row))
	{
		SelBg_Row->SetVisibility(bSelected
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
}

void UCSLedgerProductRowWidget::HandleProductButtonClicked()
{
	if (DisplayedProduct.ProductRow.DataTable != nullptr &&
		!DisplayedProduct.ProductRow.RowName.IsNone())
	{
		OnProductSelected.Broadcast(DisplayedProduct.ProductRow);
	}
}
