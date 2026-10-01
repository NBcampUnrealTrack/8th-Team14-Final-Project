#include "UI/Crafting/CSLedgerCraftInfoWidget.h"

#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"

const UCSCraftingSubsystem* UCSLedgerCraftInfoWidget::GetCrafting() const
{
	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	return IsValid(Crafting) && Crafting->IsCraftingAvailable() ? Crafting : nullptr;
}

void UCSLedgerCraftInfoWidget::ShowGuide()
{
	const UCSCraftingSubsystem* Crafting = GetCrafting();
	if (Crafting == nullptr)
	{
		HideInfo();
		return;
	}
	GuideTitleText->SetText(Crafting->GetMessageText(TEXT("LedgerCraftGuideTitle")));
	GuideBodyText1->SetText(Crafting->GetMessageText(TEXT("LedgerCraftGuideBody1")));
	GuideBodyText2->SetText(Crafting->GetMessageText(TEXT("LedgerCraftGuideBody2")));
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UCSLedgerCraftInfoWidget::HideInfo()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
