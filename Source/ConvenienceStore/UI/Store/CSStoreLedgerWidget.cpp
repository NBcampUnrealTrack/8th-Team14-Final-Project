#include "UI/Store/CSStoreLedgerWidget.h"

#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/DataTable.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
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
	DetailState = ECSLedgerDetailState::UnlockedProduct;
	bPriceLockedNoticeShown = false;

	// 목록을 다시 그려 현재 선택 행 하나에만 선택 연출이 적용되도록 합니다.
	RebuildProductList();
	if (IsValid(RightPage)) RightPage->ShowUnlocked(SelectedProduct, PendingSalePrice);
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

	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	if (!IsValid(Crafting))
	{
		static bool bWarnedMissingCrafting = false;
		if (!bWarnedMissingCrafting)
		{
			bWarnedMissingCrafting = true;
			UE_LOG(LogTemp, Warning, TEXT("[장부] 제조 서브시스템이 없어 제조 탭이 비고 꽝 상품도 숨기지 못합니다."));
		}
	}

	TArray<FCSLedgerProductViewData> Products;
	for (const FName ProductId : ProductDataTable->GetRowNames())
	{
		// 제조 전용은 판매 상품이라 전체 탭에서도 찾을 수 있어야 하고, 꽝은 장난 상품이라 장부에서 숨긴다
		const bool bCraftOnly = IsValid(Crafting) && Crafting->IsCraftOnlyResultProduct(ProductId);
		if (IsValid(Crafting) && Crafting->IsJunkResultProduct(ProductId))
		{
			continue;
		}
		if (SelectedCategory == ECSLedgerCategoryFilter::Craft ? !bCraftOnly
			: bCraftOnly && SelectedCategory != ECSLedgerCategoryFilter::All)
		{
			continue;
		}

		FCSLedgerProductViewData ProductData;
		if (BuildProductViewData(ProductId, ProductData) &&
			(SelectedCategory == ECSLedgerCategoryFilter::Craft || MatchesSelectedCategory(ProductData.Category)))
		{
			Products.Add(MoveTemp(ProductData));
		}
	}

	// 모든 상품을 이름순으로 표시합니다.
	Products.StableSort([](const FCSLedgerProductViewData& Left, const FCSLedgerProductViewData& Right)
	{
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
	// 일반 상품의 옛 레벨/평판 잠금은 사용하지 않습니다. 제조 전용 상품만
	// 아직 발견하지 못했고 재고도 없을 때 장부에서 미제조 상태로 표시합니다.
	OutData.bUnlocked = true;
	OutData.RequiredStoreLevel = 1;
	OutData.RequiredReputationGrade = ECSReputationGrade::VeryBad;
	OutData.bStoreLevelRequirementMet = true;
	OutData.bReputationRequirementMet = true;

	if (IsValid(ProductVisualRegistry))
	{
		if (const FCSProductVisualData* VisualData = ProductVisualRegistry->FindVisualData(ProductId))
		{
			OutData.ProductIcon = VisualData->ProductIcon.LoadSynchronous();
		}
	}

	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	if (const UCSStoreStockSubsystem* StockSubsystem =
		World ? World->GetSubsystem<UCSStoreStockSubsystem>() : nullptr)
	{
		OutData.CurrentStock = StockSubsystem->GetProductStock(ProductId).TotalQuantity;
	}

	if (IsValid(Crafting))
	{
		FCSCraftLedgerInfo CraftInfo;
		if (Crafting->BuildLedgerInfo(ProductId, CraftInfo))
		{
			OutData.bUnlocked = CraftInfo.bDiscovered || OutData.CurrentStock > 0;
		}
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
	case ECSLedgerCategoryFilter::Beverage:
		return ProductCategory == ECSProductCategory::Beverage ||
			ProductCategory == ECSProductCategory::HangoverCure;
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
	// 별도의 적용 버튼 없이 증감과 직접 입력이 확정되는 즉시 가격 시스템에 저장합니다.
	if (DetailState != ECSLedgerDetailState::UnlockedProduct ||
		SelectedProduct.ProductRow.DataTable == nullptr)
	{
		SetPendingSalePrice(NewSalePrice);
		return;
	}

	UWorld* World = GetWorld();
	UCSStorePricingSubsystem* PricingSubsystem =
		World ? World->GetSubsystem<UCSStorePricingSubsystem>() : nullptr;

	FText BlockedReason;
	if (IsValid(PricingSubsystem) && !PricingSubsystem->CanChangeSalePrice(BlockedReason))
	{
		// 영업 중에는 저장하지 않고 스핀박스도 원래 판매가로 되돌려 "바뀐 것처럼" 보이지 않게 함
		SetPendingSalePrice(SelectedProduct.CurrentSalePrice);

		// 스핀박스 증감 한 번에도 편집 이벤트가 여러 번 들어오므로, 상품을 바꾸기 전까진 한 번만 안내함
		if (!bPriceLockedNoticeShown)
		{
			bPriceLockedNoticeShown = true;
			if (ACSStorePlayerController* PlayerController = Cast<ACSStorePlayerController>(GetOwningPlayer()))
			{
				PlayerController->ShowNoticeBanner(BlockedReason);
			}
		}
		return;
	}

	SetPendingSalePrice(NewSalePrice);

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
