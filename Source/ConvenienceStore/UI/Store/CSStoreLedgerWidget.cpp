#include "UI/Store/CSStoreLedgerWidget.h"

#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/DataTable.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/Store/CSLedgerLeftPageWidget.h"
#include "UI/Store/CSLedgerProductRowWidget.h"
#include "UI/Store/CSLedgerRightPageWidget.h"

void UCSStoreLedgerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(LeftPage))
	{
		LeftPage->OnCategorySelected.AddUniqueDynamic(this, &ThisClass::HandleCategorySelected);
		LeftPage->OnProductSelected.AddUniqueDynamic(this, &ThisClass::HandleProductRowSelected);
	}
	if (IsValid(RightPage))
	{
		RightPage->OnSalePriceEdited.AddUniqueDynamic(this, &ThisClass::HandleSalePriceEdited);
		RightPage->OnCloseRequested.AddUniqueDynamic(this, &ThisClass::HandleRightPageCloseRequested);
	}
}

void UCSStoreLedgerWidget::NativeDestruct()
{
	if (IsValid(LeftPage))
	{
		LeftPage->OnCategorySelected.RemoveDynamic(this, &ThisClass::HandleCategorySelected);
		LeftPage->OnProductSelected.RemoveDynamic(this, &ThisClass::HandleProductRowSelected);
	}
	if (IsValid(RightPage))
	{
		RightPage->OnSalePriceEdited.RemoveDynamic(this, &ThisClass::HandleSalePriceEdited);
		RightPage->OnCloseRequested.RemoveDynamic(this, &ThisClass::HandleRightPageCloseRequested);
	}
	Super::NativeDestruct();
}

void UCSStoreLedgerWidget::OpenLedger()
{
	// 장부를 다시 열 때 이전 선택이 남지 않도록 항상 전체 보기와 미선택 페이지에서 시작합니다.
	SelectedCategory = ECSLedgerCategoryFilter::All;
	ClearSelection();

	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		World ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (IsValid(RightPage)) RightPage->SetFunds(IsValid(StoreGameState) ? StoreGameState->GetCurrentFunds() : 0);
	if (IsValid(LeftPage)) LeftPage->SetSelectedCategory(SelectedCategory);
	RebuildProductList();
}

void UCSStoreLedgerWidget::SelectCategory(ECSLedgerCategoryFilter NewCategory)
{
	if (SelectedCategory == NewCategory)
	{
		return;
	}

	// 다른 카테고리의 상품 상세가 남는 혼란을 막기 위해 카테고리 변경 시 선택을 해제합니다.
	SelectedCategory = NewCategory;
	ClearSelection();
	if (IsValid(LeftPage)) LeftPage->SetSelectedCategory(SelectedCategory);
	RebuildProductList();
}

void UCSStoreLedgerWidget::SelectProduct(const FDataTableRowHandle& ProductRow)
{
	FCSLedgerProductViewData ProductData;
	if (ProductRow.DataTable != ProductDataTable ||
		!BuildProductViewData(ProductRow.RowName, ProductData))
	{
		return;
	}

	SelectedProduct = ProductData;
	PendingSalePrice = ProductData.CurrentSalePrice;
	DetailState = ProductData.bUnlocked
		? ECSLedgerDetailState::UnlockedProduct
		: ECSLedgerDetailState::LockedProduct;

	// 목록을 다시 그려 현재 선택 행 하나에만 선택 연출이 적용되도록 합니다.
	RebuildProductList();
	if (ProductData.bUnlocked)
	{
		if (IsValid(RightPage)) RightPage->ShowUnlocked(SelectedProduct, PendingSalePrice);
	}
	else
	{
		if (IsValid(RightPage)) RightPage->ShowLocked(SelectedProduct);
	}
}

void UCSStoreLedgerWidget::ChangePendingSalePrice(int32 Delta)
{
	if (DetailState != ECSLedgerDetailState::UnlockedProduct)
	{
		return;
	}

	// 큰 증감값에도 정수 오버플로가 발생하지 않도록 넓은 타입에서 계산한 뒤 범위를 제한합니다.
	const int64 NewPrice = static_cast<int64>(PendingSalePrice) + Delta;
	SetPendingSalePrice(static_cast<int32>(FMath::Clamp<int64>(NewPrice, 0, MAX_int32)));
}

void UCSStoreLedgerWidget::SetPendingSalePrice(int32 NewSalePrice)
{
	if (DetailState != ECSLedgerDetailState::UnlockedProduct)
	{
		return;
	}

	PendingSalePrice = FMath::Max(0, NewSalePrice);
	if (IsValid(RightPage)) RightPage->SetPendingSalePriceValue(PendingSalePrice);
}

void UCSStoreLedgerWidget::RequestClose()
{
	OnLedgerCloseRequested.Broadcast();
}

void UCSStoreLedgerWidget::RebuildProductList()
{
	if (!IsValid(LeftPage) || !IsValid(ProductDataTable) || !ProductRowWidgetClass)
	{
		return;
	}

	TArray<FCSLedgerProductViewData> Products;
	for (const FName ProductId : ProductDataTable->GetRowNames())
	{
		FCSLedgerProductViewData ProductData;
		if (BuildProductViewData(ProductId, ProductData) &&
			MatchesSelectedCategory(ProductData.Category))
		{
			Products.Add(MoveTemp(ProductData));
		}
	}

	// 카테고리 안에서도 해금 상품을 먼저 보여주고 미해금 상품은 목록 아래에 모읍니다.
	Products.StableSort([](const FCSLedgerProductViewData& Left, const FCSLedgerProductViewData& Right)
	{
		if (Left.bUnlocked != Right.bUnlocked)
		{
			return Left.bUnlocked;
		}
		return Left.ProductName.ToString() < Right.ProductName.ToString();
	});

	LeftPage->SetProducts(Products, SelectedProduct.ProductId, ProductRowWidgetClass);
}

bool UCSStoreLedgerWidget::BuildProductViewData(
	FName ProductId,
	FCSLedgerProductViewData& OutData) const
{
	if (!IsValid(ProductDataTable) || ProductId.IsNone())
	{
		return false;
	}

	const FCSProductDataRow* ProductData = ProductDataTable->FindRow<FCSProductDataRow>(
		ProductId,
		TEXT("UCSStoreLedgerWidget::BuildProductViewData"));
	if (ProductData == nullptr)
	{
		return false;
	}

	// 정적 상품 정보와 재고·판매가·진행도 같은 런타임 정보를 하나의 화면 데이터로 취합합니다.
	OutData.ProductRow.DataTable = ProductDataTable;
	OutData.ProductRow.RowName = ProductId;
	OutData.ProductId = ProductId;
	OutData.ProductName = ProductData->DisplayName;
	OutData.Category = ProductData->Category;
	OutData.MarketPrice = ProductData->RecommendedSalePrice;
	OutData.PurchasePrice = ProductData->PurchasePrice;
	OutData.ShelfLifeDays = ProductData->ShelfLifeDays;
	OutData.bPerishable = ProductData->bPerishable;
	OutData.RequiredStoreLevel = ProductData->StoreLevelUnlockCondition;
	OutData.RequiredReputationGrade = ProductData->ReputationUnlockCondition;

	if (IsValid(ProductVisualRegistry))
	{
		if (const FCSProductVisualData* VisualData = ProductVisualRegistry->FindVisualData(ProductId))
		{
			OutData.ProductIcon = VisualData->ProductIcon.LoadSynchronous();
		}
	}

	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		World ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (IsValid(StoreGameState))
	{
		OutData.bStoreLevelRequirementMet =
			StoreGameState->IsStoreLevelRequirementMet(OutData.RequiredStoreLevel);
		OutData.bReputationRequirementMet =
			StoreGameState->IsReputationRequirementMet(OutData.RequiredReputationGrade);
	}
	// 상품은 상점 레벨과 평판 요구 조건을 모두 만족해야 해금된 것으로 판정합니다.
	OutData.bUnlocked =
		OutData.bStoreLevelRequirementMet && OutData.bReputationRequirementMet;

	if (const UCSStoreStockSubsystem* StockSubsystem =
		World ? World->GetSubsystem<UCSStoreStockSubsystem>() : nullptr)
	{
		OutData.CurrentStock = StockSubsystem->GetProductStock(ProductId).TotalQuantity;
	}

	if (const UCSStorePricingSubsystem* PricingSubsystem =
		World ? World->GetSubsystem<UCSStorePricingSubsystem>() : nullptr)
	{
		OutData.CurrentSalePrice = PricingSubsystem->GetSalePrice(OutData.ProductRow);
	}
	else
	{
		OutData.CurrentSalePrice = ProductData->RecommendedSalePrice;
	}

	return true;
}

bool UCSStoreLedgerWidget::MatchesSelectedCategory(ECSProductCategory ProductCategory) const
{
	// UI 필터 enum의 전체 보기는 실제 상품 카테고리에 존재하지 않으므로 별도로 처리합니다.
	if (SelectedCategory == ECSLedgerCategoryFilter::All)
	{
		return true;
	}

	switch (SelectedCategory)
	{
	case ECSLedgerCategoryFilter::Alcohol: return ProductCategory == ECSProductCategory::Alcohol;
	case ECSLedgerCategoryFilter::Beverage: return ProductCategory == ECSProductCategory::Beverage;
	case ECSLedgerCategoryFilter::FreshFood: return ProductCategory == ECSProductCategory::FreshFood;
	case ECSLedgerCategoryFilter::FrozenFood: return ProductCategory == ECSProductCategory::FrozenFood;
	case ECSLedgerCategoryFilter::Potion: return ProductCategory == ECSProductCategory::Potion;
	case ECSLedgerCategoryFilter::Scroll: return ProductCategory == ECSProductCategory::Scroll;
	case ECSLedgerCategoryFilter::Snack: return ProductCategory == ECSProductCategory::Snack;
	case ECSLedgerCategoryFilter::Supply: return ProductCategory == ECSProductCategory::Supply;
	case ECSLedgerCategoryFilter::Toiletry: return ProductCategory == ECSProductCategory::Toiletry;
	case ECSLedgerCategoryFilter::Tool: return ProductCategory == ECSProductCategory::Tool;
	default: return false;
	}
}

void UCSStoreLedgerWidget::ClearSelection()
{
	SelectedProduct = FCSLedgerProductViewData();
	PendingSalePrice = 0;
	DetailState = ECSLedgerDetailState::NoSelection;
	if (IsValid(RightPage)) RightPage->ShowEmpty();
}

void UCSStoreLedgerWidget::HandleCategorySelected(ECSLedgerCategoryFilter Category)
{
	SelectCategory(Category);
}

void UCSStoreLedgerWidget::HandleProductRowSelected(FDataTableRowHandle ProductRow)
{
	SelectProduct(ProductRow);
}

void UCSStoreLedgerWidget::HandleSalePriceEdited(int32 NewSalePrice)
{
	SetPendingSalePrice(NewSalePrice);

	// 별도의 적용 버튼 없이 증감과 직접 입력이 확정되는 즉시 가격 시스템에 저장합니다.
	if (DetailState != ECSLedgerDetailState::UnlockedProduct ||
		SelectedProduct.ProductRow.DataTable == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	UCSStorePricingSubsystem* PricingSubsystem =
		World ? World->GetSubsystem<UCSStorePricingSubsystem>() : nullptr;
	if (IsValid(PricingSubsystem) &&
		PricingSubsystem->SetSalePrice(SelectedProduct.ProductRow, PendingSalePrice))
	{
		SelectedProduct.CurrentSalePrice = PendingSalePrice;
	}
}

void UCSStoreLedgerWidget::HandleRightPageCloseRequested()
{
	RequestClose();
}
