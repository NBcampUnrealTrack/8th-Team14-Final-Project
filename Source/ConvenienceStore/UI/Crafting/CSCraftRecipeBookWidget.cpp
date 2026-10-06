#include "UI/Crafting/CSCraftRecipeBookWidget.h"

#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Features/Crafting/CSCraftStationActor.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "UI/Crafting/CSCraftListRowWidget.h"

namespace
{
	// 레시피 RowName은 Recipe_로 시작해서 겹치지 않는다
	const FName RecentAttemptsKey(TEXT("RecentAttempts"));
}

void UCSCraftRecipeBookWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
}

FReply UCSCraftRecipeBookWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Tab || InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (ACSCraftStationActor* StationActor = Station.Get())
		{
			StationActor->CloseRecipeBook();
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UCSCraftRecipeBookWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Handled();
}

const UCSCraftingSubsystem* UCSCraftRecipeBookWidget::GetCrafting() const
{
	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	return IsValid(Crafting) && Crafting->IsCraftingAvailable() ? Crafting : nullptr;
}

void UCSCraftRecipeBookWidget::SetStation(ACSCraftStationActor* InStation)
{
	Station = InStation;
	RefreshList();
}

UCSCraftListRowWidget* UCSCraftRecipeBookWidget::AddRow(
	UPanelWidget* Panel,
	FName Key,
	const FText& Name,
	const FText& Detail,
	FName IconProductId,
	bool bSelectable,
	UTexture2D* FallbackIcon)
{
	const ACSCraftStationActor* StationActor = Station.Get();
	UCSCraftListRowWidget* Row = RowClass ? CreateWidget<UCSCraftListRowWidget>(this, RowClass) : nullptr;
	if (!IsValid(Row))
	{
		return nullptr;
	}
	UTexture2D* Icon = !IconProductId.IsNone() && IsValid(StationActor)
		? StationActor->FindProductIcon(IconProductId).LoadSynchronous()
		: FallbackIcon;
	Row->SetEntry(Key, Name, Detail, -1, Icon, bSelectable,
		FCSCraftListRowSelected::CreateUObject(this, &UCSCraftRecipeBookWidget::SelectEntry));
	UPanelSlot* AddedSlot = Panel->AddChild(Row);
	if (Panel == AttemptList)
	{
		if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(AddedSlot))
		{
			ScrollSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, AttemptCardSpacing));
		}
	}
	return Row;
}

void UCSCraftRecipeBookWidget::RefreshList()
{
	RecipeList->ClearChildren();
	const ACSCraftStationActor* StationActor = Station.Get();
	const UCSCraftingSubsystem* Crafting = GetCrafting();
	if (!IsValid(StationActor) || Crafting == nullptr)
	{
		return;
	}
	if (!RowClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] WBP_CraftRecipeBook의 RowClass를 지정해 주세요."));
		return;
	}

	TitleText->SetText(StationActor->GetMessage(TEXT("RecipeBookTitle")));
	AddRow(RecipeList, RecentAttemptsKey, StationActor->GetMessage(TEXT("RecipeBookRecentAttempts")), FText::GetEmpty(),
		NAME_None, true, RecentAttemptsIcon);

	// 장부 제조 탭이 이름을 보여 주며 도감으로 안내하므로 제조 전용은 발견 전에도 이름이 보인다
	for (const FName RowName : Crafting->GetRecipeRowNames(ECSCraftRecipeKind::CraftOnly))
	{
		const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(RowName);
		const bool bShowUnknownIcon = IsValid(UnknownRecipeIcon) && !Crafting->GetDiscoveredRecipeRows().Contains(RowName);
		AddRow(RecipeList, RowName, Crafting->GetProductDisplayName(Recipe->ResultProduct), FText::GetEmpty(),
			bShowUnknownIcon ? NAME_None : Recipe->ResultProduct, true, UnknownRecipeIcon);
	}

	// 발견 전의 양념·꽝은 개수 자체가 단서가 되어 목록에 넣지 않는다
	for (const ECSCraftRecipeKind Kind : { ECSCraftRecipeKind::ExistingProduct, ECSCraftRecipeKind::Junk })
	{
		TArray<TPair<FText, FName>> Discovered;
		for (const FName RowName : Crafting->GetRecipeRowNames(Kind))
		{
			if (Crafting->GetDiscoveredRecipeRows().Contains(RowName))
			{
				const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(RowName);
				Discovered.Add({ Crafting->GetProductDisplayName(Recipe->ResultProduct), RowName });
			}
		}
		Discovered.Sort([](const TPair<FText, FName>& A, const TPair<FText, FName>& B) { return A.Key.CompareTo(B.Key) < 0; });
		for (const TPair<FText, FName>& Entry : Discovered)
		{
			const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(Entry.Value);
			AddRow(RecipeList, Entry.Value, Entry.Key, FText::GetEmpty(), Recipe->ResultProduct, true);
		}
	}

	SelectEntry(RecentAttemptsKey);
}

void UCSCraftRecipeBookWidget::SelectEntry(FName EntryKey)
{
	for (UWidget* Child : RecipeList->GetAllChildren())
	{
		if (UCSCraftListRowWidget* Row = Cast<UCSCraftListRowWidget>(Child))
		{
			Row->SetSelected(Row->GetKey() == EntryKey);
		}
	}

	if (EntryKey == RecentAttemptsKey)
	{
		ShowRecentAttempts();
	}
	else
	{
		ShowRecipeDetail(EntryKey);
	}
}

FText UCSCraftRecipeBookWidget::GetHeatName(ECSCraftHeat Heat) const
{
	const ACSCraftStationActor* StationActor = Station.Get();
	if (!IsValid(StationActor))
	{
		return FText::GetEmpty();
	}
	switch (Heat)
	{
	case ECSCraftHeat::Low: return StationActor->GetMessage(TEXT("HeatLow"));
	case ECSCraftHeat::Medium: return StationActor->GetMessage(TEXT("HeatMedium"));
	case ECSCraftHeat::High: return StationActor->GetMessage(TEXT("HeatHigh"));
	default: return FText::GetEmpty();
	}
}

FText UCSCraftRecipeBookWidget::FormatIngredientLine(
	FName MessageRowName,
	const FFormatNamedArguments& Extra,
	FName IngredientA,
	int32 CountA,
	FName IngredientB,
	int32 CountB,
	ECSCraftHeat Heat,
	int32 Stir) const
{
	const ACSCraftStationActor* StationActor = Station.Get();
	const UCSCraftingSubsystem* Crafting = GetCrafting();
	if (!IsValid(StationActor) || Crafting == nullptr)
	{
		return FText::GetEmpty();
	}
	FFormatNamedArguments Args = Extra;
	Args.Add(TEXT("IngredientA"), Crafting->GetProductDisplayName(IngredientA));
	Args.Add(TEXT("CountA"), FText::AsNumber(CountA));
	Args.Add(TEXT("IngredientB"), Crafting->GetProductDisplayName(IngredientB));
	Args.Add(TEXT("CountB"), FText::AsNumber(CountB));
	Args.Add(TEXT("Heat"), GetHeatName(Heat));
	Args.Add(TEXT("Stir"), FText::AsNumber(Stir));
	return FText::Format(FTextFormat(StationActor->GetMessage(MessageRowName)), Args);
}

void UCSCraftRecipeBookWidget::SetRumorVisible(bool bVisible)
{
	const ESlateVisibility RumorVisibility = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	for (UTextBlock* Text : { RumorMaterialLabelText.Get(), RumorMaterialText.Get(), RumorHeatLabelText.Get(),
		RumorHeatText.Get(), RumorStirLabelText.Get(), RumorStirText.Get() })
	{
		Text->SetVisibility(RumorVisibility);
	}
}

void UCSCraftRecipeBookWidget::ShowRecipeDetail(FName RecipeRowName)
{
	const ACSCraftStationActor* StationActor = Station.Get();
	const UCSCraftingSubsystem* Crafting = GetCrafting();
	const FCSCraftRecipeRow* Recipe = Crafting ? Crafting->FindRecipeByRowName(RecipeRowName) : nullptr;
	if (!IsValid(StationActor) || Recipe == nullptr)
	{
		return;
	}

	DetailSwitcher->SetActiveWidgetIndex(0);
	DetailTitleText->SetText(Crafting->GetProductDisplayName(Recipe->ResultProduct));
	if (IsValid(DetailIconImage))
	{
		UTexture2D* Icon = StationActor->FindProductIcon(Recipe->ResultProduct).LoadSynchronous();
		DetailIconImage->SetVisibility(IsValid(Icon) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (IsValid(Icon))
		{
			DetailIconImage->SetBrushFromTexture(Icon);
		}
	}

	const bool bDiscovered = Crafting->GetDiscoveredRecipeRows().Contains(RecipeRowName);
	if (bDiscovered)
	{
		FFormatNamedArguments Extra;
		Extra.Add(TEXT("PopCount"), FText::AsNumber(Recipe->PopCount));
		DetailRecipeText->SetText(FormatIngredientLine(TEXT("RecipeBookRecipeLine"), Extra,
			Recipe->IngredientA, Recipe->IngredientACount, Recipe->IngredientB, Recipe->IngredientBCount,
			Recipe->Heat, Recipe->StirCount));
	}
	else
	{
		DetailRecipeText->SetText(StationActor->GetMessage(TEXT("RecipeBookUnknown")));
	}

	if (Recipe->RecipeKind != ECSCraftRecipeKind::CraftOnly)
	{
		SetRumorVisible(false);
		return;
	}

	// 발견하면 세 칸을 모두 연다. 한 번도 안 틀리고 성공하면 영영 안 열리는 칸이 생기기 때문
	const FText Locked = StationActor->GetMessage(TEXT("RecipeBookRumorLocked"));
	SetRumorVisible(true);
	RumorMaterialLabelText->SetText(StationActor->GetMessage(TEXT("RecipeBookRumorLabelMaterial")));
	RumorHeatLabelText->SetText(StationActor->GetMessage(TEXT("RecipeBookRumorLabelHeat")));
	RumorStirLabelText->SetText(StationActor->GetMessage(TEXT("RecipeBookRumorLabelStir")));
	RumorMaterialText->SetText(Recipe->RumorStage1Text);
	RumorHeatText->SetText(bDiscovered || Crafting->IsHeatRumorOpen(RecipeRowName) ? Recipe->RumorStage2Text : Locked);
	RumorStirText->SetText(bDiscovered || Crafting->IsStirRumorOpen(RecipeRowName) ? Recipe->RumorStage3Text : Locked);
}

void UCSCraftRecipeBookWidget::ShowRecentAttempts()
{
	const ACSCraftStationActor* StationActor = Station.Get();
	const UCSCraftingSubsystem* Crafting = GetCrafting();
	if (!IsValid(StationActor) || Crafting == nullptr)
	{
		return;
	}

	DetailSwitcher->SetActiveWidgetIndex(1);
	AttemptList->ClearChildren();
	const TArray<FCSCraftAttemptRecord>& Attempts = Crafting->GetRecentAttempts();
	if (Attempts.IsEmpty())
	{
		if (UCSCraftListRowWidget* Row = AddRow(AttemptList, NAME_None,
			StationActor->GetMessage(TEXT("RecipeBookNoAttempts")), FText::GetEmpty(), NAME_None, false, RecentAttemptsIcon))
		{
			Row->SetAttemptPresentation(FText::GetEmpty(), FText::GetEmpty(), false);
		}
		return;
	}

	for (int32 Index = Attempts.Num() - 1; Index >= 0; --Index)
	{
		const FCSCraftAttemptRecord& Attempt = Attempts[Index];
		FText Result;
		FText Detail;
		if (!Attempt.ResultProduct.IsNone())
		{
			Result = Crafting->GetProductDisplayName(Attempt.ResultProduct);
		}
		else if (const FCSCraftFailureRow* FailureRow = Crafting->GetFailureRow(Attempt.Outcome))
		{
			Result = FailureRow->DisplayName;
			Detail = FailureRow->Description;
		}

		FFormatNamedArguments Extra;
		Extra.Add(TEXT("PopHits"), FText::AsNumber(Attempt.PopHits));
		Extra.Add(TEXT("PopCount"), FText::AsNumber(Attempt.PopCount));
		Extra.Add(TEXT("Result"), Result);
		const FText Ingredients = FormatIngredientLine(TEXT("RecipeBookAttemptIngredients"), Extra,
			Attempt.IngredientA, Attempt.IngredientACount, Attempt.IngredientB, Attempt.IngredientBCount,
			Attempt.Heat, Attempt.StirCount);
		const FText Method = FormatIngredientLine(TEXT("RecipeBookAttemptMethod"), Extra,
			Attempt.IngredientA, Attempt.IngredientACount, Attempt.IngredientB, Attempt.IngredientBCount,
			Attempt.Heat, Attempt.StirCount);
		const FText ResultLine = FText::Format(StationActor->GetMessage(TEXT("RecipeBookAttemptResult")), Extra);
		if (UCSCraftListRowWidget* Row = AddRow(AttemptList, NAME_None, Ingredients, Method, NAME_None, false, RecentAttemptsIcon))
		{
			const bool bSucceeded = Attempt.Outcome == ECSCraftOutcome::Success || Attempt.Outcome == ECSCraftOutcome::GreatSuccess;
			Row->SetAttemptPresentation(ResultLine, Detail, bSucceeded);
		}
	}
}
