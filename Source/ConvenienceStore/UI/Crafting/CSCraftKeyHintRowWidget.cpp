#include "UI/Crafting/CSCraftKeyHintRowWidget.h"

#include "Components/TextBlock.h"

void UCSCraftKeyHintRowWidget::SetEntry(const FCSTutorialHintEntry& Entry)
{
	KeyText->SetText(Entry.HintKey);
	HintText->SetText(Entry.HintText);
}
