#include "UI/Crafting/CSCraftIngredientPanelWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Features/Crafting/CSCraftStationActor.h"
#include "UI/Crafting/CSCraftIngredientRowWidget.h"

void UCSCraftIngredientPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	CloseButton->OnClicked.AddUniqueDynamic(this, &UCSCraftIngredientPanelWidget::HandleClose);
}

void UCSCraftIngredientPanelWidget::NativeDestruct()
{
	CloseButton->OnClicked.RemoveDynamic(this, &UCSCraftIngredientPanelWidget::HandleClose);
	Super::NativeDestruct();
}

FReply UCSCraftIngredientPanelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::E)
	{
		HandleClose();
	}
	return FReply::Handled();
}

void UCSCraftIngredientPanelWidget::SetStation(ACSCraftStationActor* InStation)
{
	Station = InStation;
	if (IsValid(InStation))
	{
		TitleText->SetText(InStation->GetMessage(TEXT("IngredientPanelTitle")));
		CloseButtonText->SetText(InStation->GetMessage(TEXT("CloseButtonLabel")));
	}
	RefreshList();
}

void UCSCraftIngredientPanelWidget::RefreshList()
{
	RowList->ClearChildren();
	MessageText->SetText(FText::GetEmpty());
	const ACSCraftStationActor* StationActor = Station.Get();
	if (!IsValid(StationActor))
	{
		return;
	}

	const TArray<FCSCraftIngredientEntry> Entries = StationActor->GetIngredientEntries();
	if (Entries.IsEmpty())
	{
		MessageText->SetText(StationActor->GetMessage(TEXT("IngredientPotEmpty")));
		return;
	}
	if (!RowClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] WBP_CraftIngredientPanel의 RowClass를 지정해 주세요."));
		return;
	}

	const FText RemoveLabel = StationActor->GetMessage(TEXT("IngredientRemoveButtonLabel"));
	for (const FCSCraftIngredientEntry& Entry : Entries)
	{
		if (UCSCraftIngredientRowWidget* Row = CreateWidget<UCSCraftIngredientRowWidget>(this, RowClass))
		{
			Row->SetEntry(Entry.ProductId, Entry.DisplayName, Entry.Count, Entry.Icon.LoadSynchronous(), RemoveLabel,
				FCSCraftIngredientRemoveClicked::CreateUObject(this, &UCSCraftIngredientPanelWidget::HandleRemove));
			RowList->AddChild(Row);
		}
	}
}

void UCSCraftIngredientPanelWidget::HandleRemove(FName ProductId)
{
	ACSCraftStationActor* StationActor = Station.Get();
	if (!IsValid(StationActor))
	{
		return;
	}
	const FText Message = StationActor->RequestRemoveIngredient(ProductId);
	RefreshList();
	// 목록을 다시 그리면 안내 칸이 비워지므로 거절 문구는 그 뒤에 넣는다
	if (!Message.IsEmpty())
	{
		MessageText->SetText(Message);
	}
}

void UCSCraftIngredientPanelWidget::HandleClose()
{
	if (ACSCraftStationActor* StationActor = Station.Get())
	{
		StationActor->CloseIngredientPanel();
	}
}
