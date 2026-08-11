#include "Systems/Pricing/CSStorePricingSubsystem.h"

#include "Data/CSProductTypes.h"

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
	if (ProductRow.DataTable == nullptr ||
		ProductRow.RowName.IsNone() ||
		NewSalePrice < 0)
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
	return true;
}
