#include "UI/Store/CSProductAnalysisWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "UI/Store/CSProductAnalysisRowWidget.h"

namespace
{
void SetSelectionVisibility(UWidget* Selected, UWidget* Default, bool bSelected)
{
	if (IsValid(Selected))
	{
		Selected->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(Default))
	{
		Default->SetVisibility(bSelected ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

FString ProductNameKey(const FText& ProductName)
{
	return ProductName.ToString();
}
}

void UCSProductAnalysisWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(AllFilterButton)) AllFilterButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleAllFilterClicked);
	if (IsValid(OutOfStockFilterButton)) OutOfStockFilterButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleOutOfStockFilterClicked);
	if (IsValid(HasWasteFilterButton)) HasWasteFilterButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleHasWasteFilterClicked);
	if (IsValid(SalesSortButton)) SalesSortButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSalesSortClicked);
	if (IsValid(AnalysisCloseButton)) AnalysisCloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseClicked);

	RefreshControlPresentation();
}

void UCSProductAnalysisWidget::NativeDestruct()
{
	if (IsValid(AllFilterButton)) AllFilterButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleAllFilterClicked);
	if (IsValid(OutOfStockFilterButton)) OutOfStockFilterButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleOutOfStockFilterClicked);
	if (IsValid(HasWasteFilterButton)) HasWasteFilterButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleHasWasteFilterClicked);
	if (IsValid(SalesSortButton)) SalesSortButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSalesSortClicked);
	if (IsValid(AnalysisCloseButton)) AnalysisCloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);

	Super::NativeDestruct();
}

void UCSProductAnalysisWidget::OpenAnalysis()
{
	ActiveFilter = EProductFilter::All;
	bSalesSortActive = false;
	bSalesSortDescending = true;
	SetVisibility(ESlateVisibility::Visible);
	RefreshAnalysis();
}

void UCSProductAnalysisWidget::RefreshAnalysis()
{
	BuildAnalysisEntries();
	RefreshSummaryCards();
	RefreshControlPresentation();
	RebuildRows();
}

void UCSProductAnalysisWidget::BuildAnalysisEntries()
{
	AnalysisEntries.Reset();
	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState = World ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(StoreGameState)) return;

	// 당일 판매 또는 폐기 이력이 있는 상품을 분석 목록에 포함합니다.
	TMap<FName, int32> EntryIndexByProductId;
	for (const FCSCheckoutTransaction& Transaction : StoreGameState->DailyCheckoutTransactions)
	{
		for (const FCSCheckoutLineItem& LineItem : Transaction.LineItems)
		{
			if (LineItem.ProductId.IsNone() || LineItem.Quantity <= 0) continue;

			int32* ExistingIndex = EntryIndexByProductId.Find(LineItem.ProductId);
			if (ExistingIndex == nullptr)
			{
				FProductAnalysisEntry& NewEntry = AnalysisEntries.AddDefaulted_GetRef();
				NewEntry.ProductId = LineItem.ProductId;
				NewEntry.ProductName = LineItem.ItemName.IsEmpty()
					? FText::FromName(LineItem.ProductId)
					: LineItem.ItemName;
				const int32 NewIndex = AnalysisEntries.Num() - 1;
				EntryIndexByProductId.Add(LineItem.ProductId, NewIndex);
				ExistingIndex = EntryIndexByProductId.Find(LineItem.ProductId);
			}

			FProductAnalysisEntry& Entry = AnalysisEntries[*ExistingIndex];
			Entry.SoldQuantity = static_cast<int32>(FMath::Min<int64>(
				static_cast<int64>(Entry.SoldQuantity) + LineItem.Quantity,
				MAX_int32));
		}
	}

	// 판매되지 않고 폐기만 된 상품도 당일 손실 기록이므로 별도 행을 만듭니다.
	for (const TPair<FName, int32>& DisposedProduct : StoreGameState->DailyDisposedProductQuantities)
	{
		if (DisposedProduct.Key.IsNone() || DisposedProduct.Value <= 0 ||
			EntryIndexByProductId.Contains(DisposedProduct.Key))
		{
			continue;
		}

		FProductAnalysisEntry& NewEntry = AnalysisEntries.AddDefaulted_GetRef();
		NewEntry.ProductId = DisposedProduct.Key;
		const FText* DisposedProductName = StoreGameState->DailyDisposedProductNames.Find(DisposedProduct.Key);
		NewEntry.ProductName = DisposedProductName && !DisposedProductName->IsEmpty()
			? *DisposedProductName
			: FText::FromName(DisposedProduct.Key);
		EntryIndexByProductId.Add(DisposedProduct.Key, AnalysisEntries.Num() - 1);
	}

	const UCSStoreStockSubsystem* StockSubsystem = World->GetSubsystem<UCSStoreStockSubsystem>();
	for (FProductAnalysisEntry& Entry : AnalysisEntries)
	{
		if (IsValid(StockSubsystem))
		{
			Entry.RemainingStock = StockSubsystem->GetProductStock(Entry.ProductId).TotalQuantity;
		}
		Entry.DisposedQuantity = StoreGameState->DailyDisposedProductQuantities.FindRef(Entry.ProductId);
	}
}

void UCSProductAnalysisWidget::RefreshSummaryCards()
{
	const ACSStoreGameState* StoreGameState = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (IsValid(AnalysisDayText))
	{
		AnalysisDayText->SetText(FText::Format(
			NSLOCTEXT("ProductAnalysis", "AnalysisDay", "{0}일차"),
			FText::AsNumber(IsValid(StoreGameState) ? StoreGameState->CurrentBusinessDay : 1)));
	}

	TArray<const FProductAnalysisEntry*> SoldEntries;
	for (const FProductAnalysisEntry& Entry : AnalysisEntries)
	{
		if (Entry.SoldQuantity > 0) SoldEntries.Add(&Entry);
	}

	if (SoldEntries.IsEmpty())
	{
		if (IsValid(PopularProductNameText)) PopularProductNameText->SetText(NSLOCTEXT("ProductAnalysis", "NoSoldProduct", "판매 상품 없음"));
		if (IsValid(PopularProductSalesText)) PopularProductSalesText->SetText(NSLOCTEXT("ProductAnalysis", "ZeroItems", "0개"));
	}
	else
	{
		int32 MaximumSales = 0;
		for (const FProductAnalysisEntry* Entry : SoldEntries) MaximumSales = FMath::Max(MaximumSales, Entry->SoldQuantity);
		TArray<const FProductAnalysisEntry*> PopularEntries;
		for (const FProductAnalysisEntry* Entry : SoldEntries)
		{
			if (Entry->SoldQuantity == MaximumSales) PopularEntries.Add(Entry);
		}
		PopularEntries.Sort([](const FProductAnalysisEntry& Left, const FProductAnalysisEntry& Right)
		{
			return ProductNameKey(Left.ProductName) < ProductNameKey(Right.ProductName);
		});
		if (IsValid(PopularProductNameText)) PopularProductNameText->SetText(MakeRepresentativeName(PopularEntries[0]->ProductName, PopularEntries.Num() - 1));
		if (IsValid(PopularProductSalesText)) PopularProductSalesText->SetText(FText::Format(NSLOCTEXT("ProductAnalysis", "ItemCount", "{0}개"), FText::AsNumber(MaximumSales)));
	}

	TArray<const FProductAnalysisEntry*> OutOfStockEntries;
	TArray<const FProductAnalysisEntry*> DisposedEntries;
	int32 TotalDisposedQuantity = 0;
	for (const FProductAnalysisEntry& Entry : AnalysisEntries)
	{
		if (Entry.RemainingStock <= 0) OutOfStockEntries.Add(&Entry);
		if (Entry.DisposedQuantity > 0)
		{
			DisposedEntries.Add(&Entry);
			TotalDisposedQuantity = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(TotalDisposedQuantity) + Entry.DisposedQuantity, MAX_int32));
		}
	}
	OutOfStockEntries.Sort([](const FProductAnalysisEntry& Left, const FProductAnalysisEntry& Right)
	{
		if (Left.SoldQuantity != Right.SoldQuantity) return Left.SoldQuantity > Right.SoldQuantity;
		return ProductNameKey(Left.ProductName) < ProductNameKey(Right.ProductName);
	});
	DisposedEntries.Sort([](const FProductAnalysisEntry& Left, const FProductAnalysisEntry& Right)
	{
		if (Left.DisposedQuantity != Right.DisposedQuantity) return Left.DisposedQuantity > Right.DisposedQuantity;
		return ProductNameKey(Left.ProductName) < ProductNameKey(Right.ProductName);
	});

	if (IsValid(OutOfStockProductNameText)) OutOfStockProductNameText->SetText(OutOfStockEntries.IsEmpty() ? NSLOCTEXT("ProductAnalysis", "NoOutOfStockProduct", "품절 상품 없음") : MakeRepresentativeName(OutOfStockEntries[0]->ProductName, OutOfStockEntries.Num() - 1));
	if (IsValid(OutOfStockCountText)) OutOfStockCountText->SetText(FText::Format(NSLOCTEXT("ProductAnalysis", "TypeCount", "{0}종"), FText::AsNumber(OutOfStockEntries.Num())));
	if (IsValid(DisposedProductNameText)) DisposedProductNameText->SetText(DisposedEntries.IsEmpty() ? NSLOCTEXT("ProductAnalysis", "NoDisposedProduct", "폐기 상품 없음") : MakeRepresentativeName(DisposedEntries[0]->ProductName, DisposedEntries.Num() - 1));
	if (IsValid(DisposedQuantityText)) DisposedQuantityText->SetText(FText::Format(NSLOCTEXT("ProductAnalysis", "ItemCount", "{0}개"), FText::AsNumber(TotalDisposedQuantity)));
	if (IsValid(DisposedTypeCountText)) DisposedTypeCountText->SetText(FText::Format(NSLOCTEXT("ProductAnalysis", "TypeCount", "{0}종"), FText::AsNumber(DisposedEntries.Num())));
}

void UCSProductAnalysisWidget::RebuildRows()
{
	if (!IsValid(ProductListScrollBox)) return;
	ProductListScrollBox->ClearChildren();
	if (!ProductAnalysisRowClass) return;

	TArray<FProductAnalysisEntry> DisplayEntries;
	for (const FProductAnalysisEntry& Entry : AnalysisEntries)
	{
		const bool bMatchesFilter = ActiveFilter == EProductFilter::All ||
			(ActiveFilter == EProductFilter::OutOfStock && Entry.RemainingStock <= 0) ||
			(ActiveFilter == EProductFilter::HasWaste && Entry.DisposedQuantity > 0);
		if (bMatchesFilter) DisplayEntries.Add(Entry);
	}

	DisplayEntries.Sort([this](const FProductAnalysisEntry& Left, const FProductAnalysisEntry& Right)
	{
		if (bSalesSortActive && Left.SoldQuantity != Right.SoldQuantity)
		{
			return bSalesSortDescending
				? Left.SoldQuantity > Right.SoldQuantity
				: Left.SoldQuantity < Right.SoldQuantity;
		}
		return ProductNameKey(Left.ProductName) < ProductNameKey(Right.ProductName);
	});

	for (const FProductAnalysisEntry& Entry : DisplayEntries)
	{
		UCSProductAnalysisRowWidget* Row = CreateWidget<UCSProductAnalysisRowWidget>(GetOwningPlayer(), ProductAnalysisRowClass);
		if (!IsValid(Row)) continue;
		Row->SetRowData(Entry.ProductName, Entry.SoldQuantity, Entry.RemainingStock, Entry.DisposedQuantity);
		ProductListScrollBox->AddChild(Row);
	}
}

void UCSProductAnalysisWidget::RefreshControlPresentation()
{
	const bool bAllSelected = ActiveFilter == EProductFilter::All;
	const bool bOutOfStockSelected = ActiveFilter == EProductFilter::OutOfStock;
	const bool bHasWasteSelected = ActiveFilter == EProductFilter::HasWaste;
	SetSelectionVisibility(AllFilterSelectedBackground, AllFilterDefaultBackground, bAllSelected);
	SetSelectionVisibility(OutOfStockFilterSelectedBackground, OutOfStockFilterDefaultBackground, bOutOfStockSelected);
	SetSelectionVisibility(HasWasteFilterSelectedBackground, HasWasteFilterDefaultBackground, bHasWasteSelected);
	SetSelectionVisibility(SalesSortSelectedBackground, SalesSortDefaultBackground, bSalesSortActive);

	if (IsValid(AllFilterText)) AllFilterText->SetColorAndOpacity(bAllSelected ? SelectedButtonTextColor : BasicButtonTextColor);
	if (IsValid(OutOfStockFilterText)) OutOfStockFilterText->SetColorAndOpacity(bOutOfStockSelected ? SelectedButtonTextColor : BasicButtonTextColor);
	if (IsValid(HasWasteFilterText)) HasWasteFilterText->SetColorAndOpacity(bHasWasteSelected ? SelectedButtonTextColor : BasicButtonTextColor);
	if (IsValid(SalesSortText)) SalesSortText->SetColorAndOpacity(bSalesSortActive ? SelectedButtonTextColor : BasicButtonTextColor);
	if (IsValid(SalesSortDirectionIcon))
	{
		SalesSortDirectionIcon->SetVisibility(bSalesSortActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		// 원본 삼각형은 위를 향하므로 내림차순 연출에서 아래 방향으로 뒤집습니다.
		SalesSortDirectionIcon->SetRenderTransformAngle(bSalesSortDescending ? 180.0f : 0.0f);
	}
}

FText UCSProductAnalysisWidget::MakeRepresentativeName(const FText& Name, int32 AdditionalTypeCount)
{
	return AdditionalTypeCount > 0
		? FText::Format(NSLOCTEXT("ProductAnalysis", "RepresentativeWithOthers", "{0} 외 {1}종"), Name, FText::AsNumber(AdditionalTypeCount))
		: Name;
}

void UCSProductAnalysisWidget::HandleAllFilterClicked()
{
	ActiveFilter = EProductFilter::All;
	RefreshControlPresentation();
	RebuildRows();
}

void UCSProductAnalysisWidget::HandleOutOfStockFilterClicked()
{
	ActiveFilter = EProductFilter::OutOfStock;
	RefreshControlPresentation();
	RebuildRows();
}

void UCSProductAnalysisWidget::HandleHasWasteFilterClicked()
{
	ActiveFilter = EProductFilter::HasWaste;
	RefreshControlPresentation();
	RebuildRows();
}

void UCSProductAnalysisWidget::HandleSalesSortClicked()
{
	// 첫 클릭은 판매량이 높은 상품부터 보여주고 이후 클릭마다 방향을 전환합니다.
	if (!bSalesSortActive)
	{
		bSalesSortActive = true;
		bSalesSortDescending = true;
	}
	else
	{
		bSalesSortDescending = !bSalesSortDescending;
	}
	RefreshControlPresentation();
	RebuildRows();
}

void UCSProductAnalysisWidget::HandleCloseClicked()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
