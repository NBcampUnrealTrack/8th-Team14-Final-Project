#include "UI/Store/CSLedgerLeftPageWidget.h"

#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "UI/Store/CSLedgerProductRowWidget.h"

namespace
{
void SetCategoryPresentation(
	UWidget* SelectedBackground,
	UTextBlock* Text,
	bool bSelected,
	const FSlateColor& DefaultTextColor,
	const FSlateColor& SelectedTextColor)
{
	if (IsValid(SelectedBackground))
	{
		SelectedBackground->SetVisibility(
			bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(Text))
	{
		Text->SetColorAndOpacity(bSelected ? SelectedTextColor : DefaultTextColor);
	}
}
}

void UCSLedgerLeftPageWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(AllCategoryButton)) AllCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleAllCategoryClicked);
	if (IsValid(AlcoholCategoryButton)) AlcoholCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleAlcoholCategoryClicked);
	if (IsValid(BeverageCategoryButton)) BeverageCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBeverageCategoryClicked);
	if (IsValid(FreshFoodCategoryButton)) FreshFoodCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleFreshFoodCategoryClicked);
	if (IsValid(FrozenFoodCategoryButton)) FrozenFoodCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleFrozenFoodCategoryClicked);
	if (IsValid(PotionCategoryButton)) PotionCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandlePotionCategoryClicked);
	if (IsValid(ScrollCategoryButton)) ScrollCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleScrollCategoryClicked);
	if (IsValid(SnackCategoryButton)) SnackCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSnackCategoryClicked);
	if (IsValid(SupplyCategoryButton)) SupplyCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSupplyCategoryClicked);
	if (IsValid(ToiletryCategoryButton)) ToiletryCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleToiletryCategoryClicked);
	if (IsValid(ToolCategoryButton)) ToolCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleToolCategoryClicked);
}

void UCSLedgerLeftPageWidget::NativeDestruct()
{
	if (IsValid(AllCategoryButton)) AllCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleAllCategoryClicked);
	if (IsValid(AlcoholCategoryButton)) AlcoholCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleAlcoholCategoryClicked);
	if (IsValid(BeverageCategoryButton)) BeverageCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleBeverageCategoryClicked);
	if (IsValid(FreshFoodCategoryButton)) FreshFoodCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleFreshFoodCategoryClicked);
	if (IsValid(FrozenFoodCategoryButton)) FrozenFoodCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleFrozenFoodCategoryClicked);
	if (IsValid(PotionCategoryButton)) PotionCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePotionCategoryClicked);
	if (IsValid(ScrollCategoryButton)) ScrollCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleScrollCategoryClicked);
	if (IsValid(SnackCategoryButton)) SnackCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSnackCategoryClicked);
	if (IsValid(SupplyCategoryButton)) SupplyCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSupplyCategoryClicked);
	if (IsValid(ToiletryCategoryButton)) ToiletryCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleToiletryCategoryClicked);
	if (IsValid(ToolCategoryButton)) ToolCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleToolCategoryClicked);

	if (IsValid(ProductListScrollBox)) ProductListScrollBox->ClearChildren();
	Super::NativeDestruct();
}

void UCSLedgerLeftPageWidget::SetProducts(
	const TArray<FCSLedgerProductViewData>& Products,
	FName SelectedProductId,
	TSubclassOf<UCSLedgerProductRowWidget> ProductRowClass)
{
	if (!IsValid(ProductListScrollBox)) return;
	ProductListScrollBox->ClearChildren();
	if (!ProductRowClass) return;

	for (const FCSLedgerProductViewData& ProductData : Products)
	{
		UCSLedgerProductRowWidget* Row =
			CreateWidget<UCSLedgerProductRowWidget>(GetOwningPlayer(), ProductRowClass);
		if (!IsValid(Row)) continue;

		Row->SetProductData(ProductData, ProductData.ProductId == SelectedProductId);
		Row->OnProductSelected.AddUniqueDynamic(this, &ThisClass::HandleProductRowSelected);
		ProductListScrollBox->AddChild(Row);
	}
}

void UCSLedgerLeftPageWidget::SetSelectedCategory(ECSLedgerCategoryFilter NewCategory)
{
	RefreshCategoryPresentation(NewCategory);
}

void UCSLedgerLeftPageWidget::BroadcastCategory(ECSLedgerCategoryFilter Category)
{
	OnCategorySelected.Broadcast(Category);
}

void UCSLedgerLeftPageWidget::RefreshCategoryPresentation(ECSLedgerCategoryFilter CurrentCategory)
{
	// 같은 규칙으로 모든 버튼을 갱신하여 선택 배경이 동시에 둘 이상 남지 않게 합니다.
	SetCategoryPresentation(SelBg_All, Txt_All, CurrentCategory == ECSLedgerCategoryFilter::All, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Alcohol, Txt_Alcohol, CurrentCategory == ECSLedgerCategoryFilter::Alcohol, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Beverage, Txt_Beverage, CurrentCategory == ECSLedgerCategoryFilter::Beverage, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Fresh, Txt_Fresh, CurrentCategory == ECSLedgerCategoryFilter::FreshFood, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Frozen, Txt_Frozen, CurrentCategory == ECSLedgerCategoryFilter::FrozenFood, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Potion, Txt_Potion, CurrentCategory == ECSLedgerCategoryFilter::Potion, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Scroll, Txt_Scroll, CurrentCategory == ECSLedgerCategoryFilter::Scroll, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Snack, Txt_Snack, CurrentCategory == ECSLedgerCategoryFilter::Snack, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Supply, Txt_Supply, CurrentCategory == ECSLedgerCategoryFilter::Supply, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Toiletry, Txt_Toiletry, CurrentCategory == ECSLedgerCategoryFilter::Toiletry, DefaultCategoryTextColor, SelectedCategoryTextColor);
	SetCategoryPresentation(SelBg_Tool, Txt_Tool, CurrentCategory == ECSLedgerCategoryFilter::Tool, DefaultCategoryTextColor, SelectedCategoryTextColor);
}

void UCSLedgerLeftPageWidget::HandleAllCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::All); }
void UCSLedgerLeftPageWidget::HandleAlcoholCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Alcohol); }
void UCSLedgerLeftPageWidget::HandleBeverageCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Beverage); }
void UCSLedgerLeftPageWidget::HandleFreshFoodCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::FreshFood); }
void UCSLedgerLeftPageWidget::HandleFrozenFoodCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::FrozenFood); }
void UCSLedgerLeftPageWidget::HandlePotionCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Potion); }
void UCSLedgerLeftPageWidget::HandleScrollCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Scroll); }
void UCSLedgerLeftPageWidget::HandleSnackCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Snack); }
void UCSLedgerLeftPageWidget::HandleSupplyCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Supply); }
void UCSLedgerLeftPageWidget::HandleToiletryCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Toiletry); }
void UCSLedgerLeftPageWidget::HandleToolCategoryClicked() { BroadcastCategory(ECSLedgerCategoryFilter::Tool); }

void UCSLedgerLeftPageWidget::HandleProductRowSelected(FDataTableRowHandle ProductRow)
{
	OnProductSelected.Broadcast(ProductRow);
}
