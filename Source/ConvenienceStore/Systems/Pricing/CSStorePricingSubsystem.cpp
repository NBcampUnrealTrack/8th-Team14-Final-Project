#include "Systems/Pricing/CSStorePricingSubsystem.h"

#include "Data/CSProductTypes.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

bool UCSStorePricingSubsystem::CanChangeSalePrice(FText& OutReason) const
{
	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState = World != nullptr ? World->GetGameState<ACSStoreGameState>() : nullptr;

	if (IsValid(StoreGameState) && StoreGameState->bIsStoreOpen)
	{
		OutReason = NSLOCTEXT("StorePricing", "PriceLockedWhileOpen", "영업시간 종료 후 변경할 수 있습니다.");
		return false;
	}

	return true;
}

int32 UCSStorePricingSubsystem::GetSalePrice(
	const FDataTableRowHandle& ProductRow) const
{
	if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
	{
		return 0;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSStorePricingSubsystem::GetSalePrice"));
	if (ProductData == nullptr)
	{
		return 0;
	}

	if (const int32* OverridePrice = SalePriceOverrides.Find(ProductRow.RowName))
	{
		return *OverridePrice;
	}

	return ProductData->RecommendedSalePrice;
}

bool UCSStorePricingSubsystem::SetSalePrice(
	const FDataTableRowHandle& ProductRow,
	int32 NewSalePrice)
{
	FText UnusedReason;
	if (ProductRow.DataTable == nullptr ||
		ProductRow.RowName.IsNone() ||
		NewSalePrice < 0 ||
		!CanChangeSalePrice(UnusedReason))
	{
		return false;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSStorePricingSubsystem::SetSalePrice"));
	if (ProductData == nullptr)
	{
		return false;
	}

	SalePriceOverrides.Add(ProductRow.RowName, NewSalePrice);

	// 실제로 저장에 성공했을 때만 방송
	OnSalePriceChanged.Broadcast(ProductRow.RowName, NewSalePrice);

	return true;
}
