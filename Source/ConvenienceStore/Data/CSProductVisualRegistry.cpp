#include "Data/CSProductVisualRegistry.h"

const FCSProductVisualData* UCSProductVisualRegistry::FindVisualData(FName ProductId) const
{
	return ProductId.IsNone() ? nullptr : ProductVisuals.Find(ProductId);
}

TSoftObjectPtr<UTexture2D> UCSProductVisualRegistry::GetProductIcon(FName ProductId) const
{
	const FCSProductVisualData* VisualData = FindVisualData(ProductId);
	return VisualData != nullptr ? VisualData->ProductIcon : TSoftObjectPtr<UTexture2D>();
}
