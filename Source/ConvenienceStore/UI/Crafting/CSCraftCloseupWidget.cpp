#include "UI/Crafting/CSCraftCloseupWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "TimerManager.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "UI/Crafting/CSCraftKeyHintRowWidget.h"

void UCSCraftCloseupWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 포커스를 가져가면 클로즈업 입력이 안 들어온다
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
	MessageText->SetVisibility(ESlateVisibility::Collapsed);
}

void UCSCraftCloseupWidget::NativeDestruct()
{
	// 클로즈업을 나간 뒤 죽은 위젯에 타이머가 발화하지 않게 한다
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MessageHideTimerHandle);
	}
	Super::NativeDestruct();
}

void UCSCraftCloseupWidget::ShowMessage(const FText& Message, bool bWarning)
{
	MessageText->SetText(Message);
	MessageText->SetColorAndOpacity(bWarning ? WarningMessageColor : InfoMessageColor);
	MessageText->SetVisibility(ESlateVisibility::HitTestInvisible);

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			MessageHideTimerHandle, this, &UCSCraftCloseupWidget::HideMessage, MessageDisplaySeconds, false);
	}
}

void UCSCraftCloseupWidget::HideMessage()
{
	MessageText->SetVisibility(ESlateVisibility::Collapsed);
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
