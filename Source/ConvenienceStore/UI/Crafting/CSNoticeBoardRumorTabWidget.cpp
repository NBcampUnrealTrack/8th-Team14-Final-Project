#include "UI/Crafting/CSNoticeBoardRumorTabWidget.h"

#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Systems/Crafting/CSCraftRumorSubsystem.h"

void UCSNoticeBoardRumorTabWidget::NativeConstruct()
{
	Super::NativeConstruct();

	const UWorld* World = GetWorld();
	if (UCSCraftRumorSubsystem* Rumor = World ? World->GetSubsystem<UCSCraftRumorSubsystem>() : nullptr)
	{
		RumorSubsystem = Rumor;
		Rumor->OnCraftRumorsChanged.AddUniqueDynamic(this, &UCSNoticeBoardRumorTabWidget::RefreshRumors);
	}
	RefreshRumors();
}

void UCSNoticeBoardRumorTabWidget::NativeDestruct()
{
	if (UCSCraftRumorSubsystem* Rumor = RumorSubsystem.Get())
	{
		Rumor->OnCraftRumorsChanged.RemoveDynamic(this, &UCSNoticeBoardRumorTabWidget::RefreshRumors);
	}
	RumorSubsystem.Reset();
	Super::NativeDestruct();
}

void UCSNoticeBoardRumorTabWidget::RefreshRumors()
{
	UCSCraftRumorSubsystem* Rumor = RumorSubsystem.Get();
	const TArray<FText> Rumors = IsValid(Rumor) ? Rumor->GetCurrentRumors() : TArray<FText>();
	UTextBlock* const Texts[] = { RumorText1.Get(), RumorText2.Get(), RumorText3.Get() };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Texts); ++Index)
	{
		Texts[Index]->SetText(Rumors.IsValidIndex(Index) ? Rumors[Index] : FText::GetEmpty());
	}
}
