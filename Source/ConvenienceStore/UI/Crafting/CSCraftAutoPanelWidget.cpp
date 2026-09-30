#include "UI/Crafting/CSCraftAutoPanelWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UI/Crafting/CSCraftListRowWidget.h"

void UCSCraftAutoPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	CountUpButton->OnClicked.AddUniqueDynamic(this, &UCSCraftAutoPanelWidget::HandleCountUp);
	CountDownButton->OnClicked.AddUniqueDynamic(this, &UCSCraftAutoPanelWidget::HandleCountDown);
	CraftButton->OnClicked.AddUniqueDynamic(this, &UCSCraftAutoPanelWidget::HandleCraft);
	CloseButton->OnClicked.AddUniqueDynamic(this, &UCSCraftAutoPanelWidget::HandleClose);
}

void UCSCraftAutoPanelWidget::NativeDestruct()
{
	CountUpButton->OnClicked.RemoveDynamic(this, &UCSCraftAutoPanelWidget::HandleCountUp);
	CountDownButton->OnClicked.RemoveDynamic(this, &UCSCraftAutoPanelWidget::HandleCountDown);
	CraftButton->OnClicked.RemoveDynamic(this, &UCSCraftAutoPanelWidget::HandleCraft);
	CloseButton->OnClicked.RemoveDynamic(this, &UCSCraftAutoPanelWidget::HandleClose);
	Super::NativeDestruct();
}

FReply UCSCraftAutoPanelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::R)
	{
		HandleClose();
	}
	return FReply::Handled();
}

void UCSCraftAutoPanelWidget::SetStation(ACSCraftStationActor* InStation)
{
	Station = InStation;
	if (IsValid(InStation))
	{
		TitleText->SetText(InStation->GetMessage(TEXT("AutoCraftTitle")));
		CraftButtonText->SetText(InStation->GetMessage(TEXT("AutoCraftButtonLabel")));
		CloseButtonText->SetText(InStation->GetMessage(TEXT("CloseButtonLabel")));
	}
	RefreshList();
}

void UCSCraftAutoPanelWidget::RefreshList()
{
	RowList->ClearChildren();
	const ACSCraftStationActor* StationActor = Station.Get();
	Options = IsValid(StationActor) ? StationActor->GetAutoCraftOptions() : TArray<FCSCraftAutoOption>();
	MessageText->SetText(FText::GetEmpty());

	if (!RowClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] WBP_CraftAutoPanel의 RowClass를 지정해 주세요."));
	}
	else if (IsValid(StationActor))
	{
		const FText IngredientFormat = StationActor->GetMessage(TEXT("AutoCraftIngredientFormat"));
		for (const FCSCraftAutoOption& Option : Options)
		{
			const FText LineA = FText::Format(IngredientFormat,
				Option.IngredientAName, FText::AsNumber(Option.IngredientANeed), FText::AsNumber(Option.IngredientAOwned));
			const FText LineB = FText::Format(IngredientFormat,
				Option.IngredientBName, FText::AsNumber(Option.IngredientBNeed), FText::AsNumber(Option.IngredientBOwned));
			const FText IngredientLine = FText::Join(FText::FromString(TEXT("\n")), TArray<FText>{ LineA, LineB });

			if (UCSCraftListRowWidget* Row = CreateWidget<UCSCraftListRowWidget>(this, RowClass))
			{
				// 재료가 한 세트도 안 되는 레시피는 보여 주되 고를 수 없다
				Row->SetEntry(Option.RecipeRowName, Option.ResultName, IngredientLine, Option.MaxSets,
					Option.ResultIcon.LoadSynchronous(), Option.MaxSets > 0,
					FCSCraftListRowSelected::CreateUObject(this, &UCSCraftAutoPanelWidget::SelectRecipe));
				RowList->AddChild(Row);
			}
		}
	}
	RefreshSelection();
}

void UCSCraftAutoPanelWidget::SelectRecipe(FName RecipeRowName)
{
	if (SelectedRecipeRowName != RecipeRowName)
	{
		SelectedRecipeRowName = RecipeRowName;
		SelectedSets = 1;
	}
	RefreshSelection();
}

const FCSCraftAutoOption* UCSCraftAutoPanelWidget::FindSelectedOption() const
{
	return Options.FindByPredicate([this](const FCSCraftAutoOption& Option)
	{
		return Option.RecipeRowName == SelectedRecipeRowName && Option.MaxSets > 0;
	});
}

void UCSCraftAutoPanelWidget::RefreshSelection()
{
	const FCSCraftAutoOption* Selected = FindSelectedOption();
	if (Selected == nullptr)
	{
		SelectedRecipeRowName = NAME_None;
	}
	const int32 MaxSets = Selected ? Selected->MaxSets : 1;
	SelectedSets = FMath::Clamp(SelectedSets, 1, FMath::Max(1, MaxSets));

	SelectedNameText->SetText(Selected ? Selected->ResultName : FText::GetEmpty());
	CountText->SetText(Selected ? FText::AsNumber(SelectedSets) : FText::GetEmpty());
	CountUpButton->SetIsEnabled(Selected != nullptr && SelectedSets < MaxSets);
	CountDownButton->SetIsEnabled(Selected != nullptr && SelectedSets > 1);
	CraftButton->SetIsEnabled(Selected != nullptr);
}

void UCSCraftAutoPanelWidget::HandleCountUp()
{
	++SelectedSets;
	RefreshSelection();
}

void UCSCraftAutoPanelWidget::HandleCountDown()
{
	--SelectedSets;
	RefreshSelection();
}

void UCSCraftAutoPanelWidget::HandleCraft()
{
	ACSCraftStationActor* StationActor = Station.Get();
	if (!IsValid(StationActor) || SelectedRecipeRowName.IsNone())
	{
		return;
	}
	// 만들었으면 제조대가 이 창을 닫는다
	const FText Message = StationActor->RequestAutoCraft(SelectedRecipeRowName, SelectedSets);
	if (!Message.IsEmpty())
	{
		RefreshList();
		MessageText->SetText(Message);
	}
}

void UCSCraftAutoPanelWidget::HandleClose()
{
	if (ACSCraftStationActor* StationActor = Station.Get())
	{
		StationActor->CloseAutoPanel();
	}
}
