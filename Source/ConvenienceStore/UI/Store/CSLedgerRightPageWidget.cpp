#include "UI/Store/CSLedgerRightPageWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"

namespace
{
FText GetLedgerCategoryText(ECSProductCategory Category)
{
	switch (Category)
	{
	case ECSProductCategory::Alcohol: return FText::FromString(TEXT("주류"));
	case ECSProductCategory::Beverage: return FText::FromString(TEXT("음료"));
	case ECSProductCategory::FreshFood: return FText::FromString(TEXT("신선식품"));
	case ECSProductCategory::FrozenFood: return FText::FromString(TEXT("냉동식품"));
	case ECSProductCategory::Potion: return FText::FromString(TEXT("포션"));
	case ECSProductCategory::Scroll: return FText::FromString(TEXT("두루마리"));
	case ECSProductCategory::Snack: return FText::FromString(TEXT("간식"));
	case ECSProductCategory::Supply: return FText::FromString(TEXT("보급품"));
	case ECSProductCategory::Toiletry: return FText::FromString(TEXT("위생용품"));
	case ECSProductCategory::Tool: return FText::FromString(TEXT("도구"));
	default: return FText::GetEmpty();
	}
}

FText GetLedgerReputationText(ECSReputationGrade Grade)
{
	switch (Grade)
	{
	case ECSReputationGrade::VeryBad: return NSLOCTEXT("StoreLedger", "ReputationVeryBad", "매우 나쁨");
	case ECSReputationGrade::Bad: return NSLOCTEXT("StoreLedger", "ReputationBad", "나쁨");
	case ECSReputationGrade::Normal: return NSLOCTEXT("StoreLedger", "ReputationNormal", "보통");
	case ECSReputationGrade::Good: return NSLOCTEXT("StoreLedger", "ReputationGood", "좋음");
	case ECSReputationGrade::Excellent: return NSLOCTEXT("StoreLedger", "ReputationExcellent", "매우 좋음");
	case ECSReputationGrade::None:
	default: return NSLOCTEXT("StoreLedger", "NoReputationRequirement", "조건 없음");
	}
}
}

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
	if (IsValid(ProductStatusSwitcher)) ProductStatusSwitcher->SetActiveWidgetIndex(0);
	RefreshProductHeader(ProductData);
	RefreshUnlockedMetrics(ProductData);
	ShowUnlockedProductDetail(ProductData);
	SetPendingSalePriceValue(PendingSalePrice);
}

void UCSLedgerRightPageWidget::ShowLocked(const FCSLedgerProductViewData& ProductData)
{
	if (IsValid(SelectionSwitcher)) SelectionSwitcher->SetActiveWidgetIndex(1);
	if (IsValid(ProductStatusSwitcher)) ProductStatusSwitcher->SetActiveWidgetIndex(1);
	RefreshProductHeader(ProductData);
	RefreshLockedConditions(ProductData);
	ShowLockedProductDetail(ProductData);
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
	if (IsValid(Txt_Category)) Txt_Category->SetText(GetLedgerCategoryText(ProductData.Category));
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

void UCSLedgerRightPageWidget::RefreshLockedConditions(const FCSLedgerProductViewData& ProductData)
{
	if (IsValid(Txt_LevelReq))
	{
		Txt_LevelReq->SetText(FText::Format(
			NSLOCTEXT("StoreLedger", "RequiredStoreLevel", "상점 레벨 {0}"),
			FText::AsNumber(ProductData.RequiredStoreLevel)));
	}
	if (IsValid(Txt_ReputationReq))
	{
		Txt_ReputationReq->SetText(FText::Format(
			NSLOCTEXT("StoreLedger", "RequiredReputation", "평판 {0}"),
			GetLedgerReputationText(ProductData.RequiredReputationGrade)));
	}

	// 요구 조건과 달성 상태를 분리해 디자이너가 행 배치를 바꿔도 데이터 연결은 유지됩니다.
	auto SetConditionStatus = [this](UTextBlock* StatusText, bool bRequirementMet)
	{
		if (!IsValid(StatusText)) return;
		StatusText->SetText(bRequirementMet
			? NSLOCTEXT("StoreLedger", "RequirementMet", "달성")
			: NSLOCTEXT("StoreLedger", "RequirementUnmet", "미달성"));
		StatusText->SetColorAndOpacity(bRequirementMet
			? FSlateColor(MetConditionColor)
			: FSlateColor(UnmetConditionColor));
	};

	SetConditionStatus(Txt_LevelStatus, ProductData.bStoreLevelRequirementMet);
	SetConditionStatus(Txt_ReputationStatus, ProductData.bReputationRequirementMet);
}
