#include "Features/Customers/CSInnerThoughtWidget.h"
#include "Components/TextBlock.h"

void UCSInnerThoughtWidget::SetThoughtText(const FText& InText)
{
	if (IsValid(ThoughtText))
	{
		ThoughtText->SetText(InText);
	}
}
