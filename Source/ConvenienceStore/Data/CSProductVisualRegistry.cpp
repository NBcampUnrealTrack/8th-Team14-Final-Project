#include "Data/CSProductVisualRegistry.h"

const FCSProductVisualData* UCSProductVisualRegistry::FindVisualData(FName ProductId) const
{
	return ProductId.IsNone() ? nullptr : ProductVisuals.Find(ProductId);
}
