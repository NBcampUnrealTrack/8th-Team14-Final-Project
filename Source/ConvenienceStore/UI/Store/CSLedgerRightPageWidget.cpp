#include "UI/Store/CSLedgerRightPageWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "UI/Crafting/CSLedgerCraftInfoWidget.h"

void UCSLedgerRightPageWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(DecreaseSalePriceButton)) DecreaseSalePriceButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDecreaseClicked);
	if (IsValid(IncreaseSalePriceButton)) IncreaseSalePriceButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleIncreaseClicked);
	if (IsValid(CloseButton)) CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseClicked);
	if (IsValid(SalePriceSpinBox)) SalePriceSpinBox->OnValueCommitted.AddUniqueDynamic(this, &ThisClass::HandleSalePriceCommitted);
}

void UCSLedgerRightPageWidget::NativeDestruct()
{
	if (IsValid(DecreaseSalePriceButton)) DecreaseSalePriceButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleDecreaseClicked);
	if (IsValid(IncreaseSalePriceButton)) IncreaseSalePriceButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleIncreaseClicked);
	if (IsValid(CloseButton)) CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	if (IsValid(SalePriceSpinBox)) SalePriceSpinBox->OnValueCommitted.RemoveDynamic(this, &ThisClass::HandleSalePriceCommitted);
	Super::NativeDestruct();
}

void UCSLedgerRightPageWidget::ShowEmpty()
{
	if (IsValid(SelectionSwitcher)) SelectionSwitcher->SetActiveWidgetIndex(0);
}

void UCSLedgerRightPageWidget::SetFunds(int32 CurrentFunds)
{
	if (IsValid(Txt_Funds)) Txt_Funds->SetText(FText::AsNumber(CurrentFunds));
}

void UCSLedgerRightPageWidget::ShowUnlocked(
	const FCSLedgerProductViewData& ProductData,
	int32 PendingSalePrice)
{
	if (IsValid(SelectionSwitcher)) SelectionSwitcher->SetActiveWidgetIndex(1);
	RefreshProductHeader(ProductData);

	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	FCSCraftLedgerInfo CraftInfo;
	const bool bCraftOnly = IsValid(Crafting) && Crafting->BuildLedgerInfo(ProductData.ProductId, CraftInfo);

	// 재고까지 보는 건 도감이 저장되지 않아 불러오면 발견 기록이 사라져도, 창고에 물건이 있으면 판매가를 바꿀 수 있어야 해서다
	if (bCraftOnly && !CraftInfo.bDiscovered && ProductData.CurrentStock <= 0)
	{
		if (IsValid(ProductStatusSwitcher)) ProductStatusSwitcher->SetActiveWidgetIndex(1);
		// 옛 해금 칸 텍스트는 WBP 직렬화 때문에 남겨 두고 보이지만 않게 한다
		for (UTextBlock* OldText : { Txt_LevelReq.Get(), Txt_LevelStatus.Get(), Txt_ReputationReq.Get(), Txt_ReputationStatus.Get() })
		{
			if (IsValid(OldText)) OldText->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (IsValid(CraftGuideInfo)) CraftGuideInfo->ShowGuide();
		SetPendingSalePriceValue(PendingSalePrice);
		return;
	}

	if (IsValid(ProductStatusSwitcher)) ProductStatusSwitcher->SetActiveWidgetIndex(0);
	if (IsValid(CraftGuideInfo)) CraftGuideInfo->HideInfo();
	RefreshUnlockedMetrics(ProductData);
	ShowUnlockedProductDetail(ProductData);
	SetPendingSalePriceValue(PendingSalePrice);
}

void UCSLedgerRightPageWidget::ShowLocked(const FCSLedgerProductViewData& ProductData)
{
	// 기존 진입점도 잠금 화면 대신 일반 상품 상세를 표시합니다.
	ShowUnlocked(ProductData, ProductData.CurrentSalePrice);
}

void UCSLedgerRightPageWidget::SetPendingSalePriceValue(int32 NewSalePrice)
{
	if (IsValid(SalePriceSpinBox))
	{
		SalePriceSpinBox->SetValue(static_cast<float>(NewSalePrice));
	}
	RefreshPendingSalePriceView(NewSalePrice);
}

void UCSLedgerRightPageWidget::HandleDecreaseClicked()
{
	const int32 CurrentValue = IsValid(SalePriceSpinBox)
		? FMath::RoundToInt(SalePriceSpinBox->GetValue())
		: 0;
	OnSalePriceEdited.Broadcast(CurrentValue - 1);
}

void UCSLedgerRightPageWidget::HandleIncreaseClicked()
{
	const int32 CurrentValue = IsValid(SalePriceSpinBox)
		? FMath::RoundToInt(SalePriceSpinBox->GetValue())
		: 0;
	OnSalePriceEdited.Broadcast(CurrentValue + 1);
}

void UCSLedgerRightPageWidget::HandleCloseClicked()
{
	OnCloseRequested.Broadcast();
}

void UCSLedgerRightPageWidget::HandleSalePriceCommitted(float Value, ETextCommit::Type CommitMethod)
{
	OnSalePriceEdited.Broadcast(FMath::RoundToInt(Value));
}

void UCSLedgerRightPageWidget::RefreshProductHeader(const FCSLedgerProductViewData& ProductData)
{
	if (IsValid(Txt_ProductName)) Txt_ProductName->SetText(ProductData.ProductName);
	if (IsValid(Txt_Category)) Txt_Category->SetText(CSGetProductCategoryDisplayName(ProductData.Category));
	if (IsValid(Img_Product))
	{
		Img_Product->SetBrushFromTexture(ProductData.ProductIcon, true);
		Img_Product->SetVisibility(IsValid(ProductData.ProductIcon)
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Hidden);
	}
}

void UCSLedgerRightPageWidget::RefreshUnlockedMetrics(const FCSLedgerProductViewData& ProductData)
{
	// 단위와 값의 조합을 C++에서 통일하여 각 WBP가 서로 다른 표기법을 만들지 않게 합니다.
	if (IsValid(Txt_Stock))
	{
		Txt_Stock->SetText(FText::Format(
			NSLOCTEXT("StoreLedger", "StockCount", "{0}개"),
			FText::AsNumber(ProductData.CurrentStock)));
	}
	if (IsValid(Txt_MarketPrice))
	{
		Txt_MarketPrice->SetText(FText::AsNumber(ProductData.MarketPrice));
	}
	if (IsValid(Txt_PurchasePrice))
	{
		Txt_PurchasePrice->SetText(FText::AsNumber(ProductData.PurchasePrice));
	}
	if (IsValid(Txt_ShelfLife))
	{
		Txt_ShelfLife->SetText(ProductData.bPerishable
			? FText::Format(
				NSLOCTEXT("StoreLedger", "ShelfLifeDays", "{0}일"),
				FText::AsNumber(ProductData.ShelfLifeDays))
			: NSLOCTEXT("StoreLedger", "NoShelfLife", "없음"));
	}
}
