#include "UI/Crafting/CSCraftCloseupWidget.h"

#include "Components/PanelWidget.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "UI/Crafting/CSCraftKeyHintRowWidget.h"

void UCSCraftCloseupWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 포커스를 가져가면 클로즈업 입력이 안 들어온다
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
}

void UCSCraftCloseupWidget::SetStep(ECSCraftStep Step)
{
	KeyHintList->ClearChildren();

	if (!KeyHintRowClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] WBP_CraftCloseup의 KeyHintRowClass를 지정해 주세요."));
		return;
	}

	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	if (!IsValid(Crafting))
	{
		return;
	}

	for (const FCSTutorialHintEntry& Hint : Crafting->GetKeyHints(Step))
	{
		if (UCSCraftKeyHintRowWidget* Row = CreateWidget<UCSCraftKeyHintRowWidget>(this, KeyHintRowClass))
		{
			Row->SetEntry(Hint);
			KeyHintList->AddChild(Row);
		}
	}
}
