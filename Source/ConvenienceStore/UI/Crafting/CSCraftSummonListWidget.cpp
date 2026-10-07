#include "UI/Crafting/CSCraftSummonListWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UI/Crafting/CSCraftListRowWidget.h"

void UCSCraftSummonListWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	CountUpButton->OnClicked.AddUniqueDynamic(this, &UCSCraftSummonListWidget::HandleCountUp);
	CountDownButton->OnClicked.AddUniqueDynamic(this, &UCSCraftSummonListWidget::HandleCountDown);
	SummonButton->OnClicked.AddUniqueDynamic(this, &UCSCraftSummonListWidget::HandleSummon);
	CloseButton->OnClicked.AddUniqueDynamic(this, &UCSCraftSummonListWidget::HandleClose);
}

void UCSCraftSummonListWidget::NativeDestruct()
{
	CountUpButton->OnClicked.RemoveDynamic(this, &UCSCraftSummonListWidget::HandleCountUp);
	CountDownButton->OnClicked.RemoveDynamic(this, &UCSCraftSummonListWidget::HandleCountDown);
	SummonButton->OnClicked.RemoveDynamic(this, &UCSCraftSummonListWidget::HandleSummon);
	CloseButton->OnClicked.RemoveDynamic(this, &UCSCraftSummonListWidget::HandleClose);
	Super::NativeDestruct();
}

FReply UCSCraftSummonListWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::E)
	{
		HandleClose();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FText UCSCraftSummonListWidget::GetMessage(FName MessageRowName) const
{
	const ACSCraftStationActor* StationActor = Station.Get();
	return IsValid(StationActor) ? StationActor->GetMessage(MessageRowName) : FText::GetEmpty();
}

void UCSCraftSummonListWidget::SetStation(ACSCraftStationActor* InStation)
{
	Station = InStation;
	TitleText->SetText(GetMessage(TEXT("SummonTitle")));
	SummonButtonText->SetText(GetMessage(TEXT("SummonButtonLabel")));
	CloseButtonText->SetText(GetMessage(TEXT("CloseButtonLabel")));
	RefreshList();
}

void UCSCraftSummonListWidget::RefreshList()
{
	RowList->ClearChildren();
	const ACSCraftStationActor* StationActor = Station.Get();
	Stocks = IsValid(StationActor) ? StationActor->GetStorageStockList() : TArray<FCSCraftStorageStock>();

	// 창고에서 줄어든 만큼 담은 개수도 줄인다
	for (auto It = SelectedCounts.CreateIterator(); It; ++It)
	{
		It.Value() = FMath::Min(It.Value(), GetMaxCount(It.Key()));
		if (It.Value() <= 0)
		{
			It.RemoveCurrent();
		}
	}
	if (FindStock(FocusedProductId) == nullptr)
	{
		FocusedProductId = NAME_None;
	}

	MessageText->SetText(Stocks.IsEmpty() ? GetMessage(TEXT("SummonStorageEmpty")) : FText::GetEmpty());

	if (!RowClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] WBP_CraftSummonList의 RowClass를 지정해 주세요."));
	}
	else
	{
		const FText SelectedFormat = GetMessage(TEXT("SummonRowSelected"));
		for (const FCSCraftStorageStock& Stock : Stocks)
		{
			const int32* Selected = SelectedCounts.Find(Stock.ProductId);
			const FText Detail = Selected ? FText::Format(SelectedFormat, FText::AsNumber(*Selected)) : FText::GetEmpty();
			if (UCSCraftListRowWidget* Row = CreateWidget<UCSCraftListRowWidget>(this, RowClass))
			{
				Row->SetEntry(Stock.ProductId, Stock.DisplayName, Detail, Stock.Count, Stock.Icon.LoadSynchronous(), true,
					FCSCraftListRowSelected::CreateUObject(this, &UCSCraftSummonListWidget::SelectProduct));
				// +/-가 적용되는 줄만 표시한다. 담긴 다른 줄은 설명 칸의 "n개 선택"으로 보인다
				Row->SetSelected(Stock.ProductId == FocusedProductId);
				RowList->AddChild(Row);
			}
		}
	}
	RefreshSelection();
}

void UCSCraftSummonListWidget::SelectProduct(FName ProductId)
{
	FocusedProductId = ProductId;
	if (!SelectedCounts.Contains(ProductId))
	{
		if (SelectedCounts.Num() >= ACSCraftStationActor::MaxSummonKinds)
		{
			MessageText->SetText(FText::Format(GetMessage(TEXT("SummonTooManyKinds")), FText::AsNumber(ACSCraftStationActor::MaxSummonKinds)));
			RefreshSelection();
			return;
		}
		SelectedCounts.Add(ProductId, 1);
	}
	RefreshList();
}

const FCSCraftStorageStock* UCSCraftSummonListWidget::FindStock(FName ProductId) const
{
	return Stocks.FindByPredicate([ProductId](const FCSCraftStorageStock& Stock) { return Stock.ProductId == ProductId; });
}

int32 UCSCraftSummonListWidget::GetMaxCount(FName ProductId) const
{
	const FCSCraftStorageStock* Stock = FindStock(ProductId);
	return Stock ? FMath::Min(ACSCraftStationActor::MaxCircleQuantity, Stock->Count) : 0;
}

void UCSCraftSummonListWidget::RefreshSelection()
{
	TArray<FText> Items;
	const FText ItemFormat = GetMessage(TEXT("SummonSelectionItem"));
	for (const FCSCraftStorageStock& Stock : Stocks)
	{
		if (const int32* Selected = SelectedCounts.Find(Stock.ProductId))
		{
			Items.Add(FText::Format(ItemFormat, Stock.DisplayName, FText::AsNumber(*Selected)));
		}
	}
	SelectedNameText->SetText(Items.IsEmpty() ? GetMessage(TEXT("SummonSelectNone")) : FText::Join(FText::FromString(TEXT("\n")), Items));
	FSlateFontInfo SelectionFont = SelectedNameText->GetFont();
	SelectionFont.TypefaceFontName = Items.IsEmpty() ? PlaceholderTypeface : SelectedTypeface;
	SelectedNameText->SetFont(SelectionFont);

	const int32* FocusedCount = SelectedCounts.Find(FocusedProductId);
	CountText->SetText(FocusedCount ? FText::AsNumber(*FocusedCount) : FText::GetEmpty());
	CountUpButton->SetIsEnabled(FocusedCount != nullptr && *FocusedCount < GetMaxCount(FocusedProductId));
	CountDownButton->SetIsEnabled(FocusedCount != nullptr);
	SummonButton->SetIsEnabled(!SelectedCounts.IsEmpty());
}

void UCSCraftSummonListWidget::HandleCountUp()
{
	if (int32* Count = SelectedCounts.Find(FocusedProductId))
	{
		*Count = FMath::Min(*Count + 1, GetMaxCount(FocusedProductId));
		RefreshList();
	}
}

void UCSCraftSummonListWidget::HandleCountDown()
{
	if (int32* Count = SelectedCounts.Find(FocusedProductId))
	{
		if (--(*Count) <= 0)
		{
			SelectedCounts.Remove(FocusedProductId);
		}
		RefreshList();
	}
}

void UCSCraftSummonListWidget::HandleSummon()
{
	ACSCraftStationActor* StationActor = Station.Get();
	if (!IsValid(StationActor) || SelectedCounts.IsEmpty())
	{
		return;
	}
	bool bSummoned = false;
	const FText Message = StationActor->RequestSummon(SelectedCounts, bSummoned);
	if (bSummoned)
	{
		SelectedCounts.Reset();
		FocusedProductId = NAME_None;
	}
	RefreshList();
	MessageText->SetText(Message);
}

void UCSCraftSummonListWidget::HandleClose()
{
	if (ACSCraftStationActor* StationActor = Station.Get())
	{
		StationActor->CloseSummonList();
	}
}
