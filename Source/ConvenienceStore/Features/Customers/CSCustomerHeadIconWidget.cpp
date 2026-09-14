#include "Features/Customers/CSCustomerHeadIconWidget.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

void UCSCustomerHeadIconWidget::SetIcon(UTexture2D* InIcon)
{
	if (!IsValid(IconImage))
	{
		return;
	}

	IconImage->SetBrushFromTexture(InIcon);
}
