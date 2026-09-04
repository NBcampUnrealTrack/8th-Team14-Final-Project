#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSLedgerTypes.h"
#include "CSLedgerLeftPageWidget.generated.h"

class UButton;
class UCSLedgerProductRowWidget;
class UScrollBox;
class UTextBlock;
class UWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnLedgerCategorySelected,
	ECSLedgerCategoryFilter,
	Category);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnLedgerLeftPageProductSelected,
	FDataTableRowHandle,
	ProductRow);

UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSLedgerLeftPageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 메인 장부가 필터링·정렬한 결과를 전달하면 왼쪽 페이지는 행 생성과 표현만 담당합니다.
	void SetProducts(
		const TArray<FCSLedgerProductViewData>& Products,
		FName SelectedProductId,
		TSubclassOf<UCSLedgerProductRowWidget> ProductRowClass);

	void SetSelectedCategory(ECSLedgerCategoryFilter NewCategory);

	UPROPERTY(BlueprintAssignable, Category = "Store|Ledger")
	FCSOnLedgerCategorySelected OnCategorySelected;

	UPROPERTY(BlueprintAssignable, Category = "Store|Ledger")
	FCSOnLedgerLeftPageProductSelected OnProductSelected;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UScrollBox> ProductListScrollBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> AllCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> AlcoholCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> BeverageCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> FreshFoodCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> FrozenFoodCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> PotionCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ScrollCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SnackCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SupplyCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ToiletryCategoryButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ToolCategoryButton;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_All;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Alcohol;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Beverage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Fresh;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Frozen;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Potion;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Scroll;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Snack;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Supply;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Toiletry;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Tool;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_All;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Alcohol;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Beverage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Fresh;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Frozen;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Potion;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Scroll;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Snack;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Supply;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Toiletry;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Tool;

	// 디자이너가 WBP 기본값에서 장부 시안에 맞는 두 글자색을 지정할 수 있게 합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Style")
	FSlateColor DefaultCategoryTextColor = FSlateColor(FLinearColor(0.22f, 0.15f, 0.08f, 1.0f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Style")
	FSlateColor SelectedCategoryTextColor = FSlateColor(FLinearColor::White);

private:
	void BroadcastCategory(ECSLedgerCategoryFilter Category);
	void RefreshCategoryPresentation(ECSLedgerCategoryFilter CurrentCategory);

	UFUNCTION() void HandleAllCategoryClicked();
	UFUNCTION() void HandleAlcoholCategoryClicked();
	UFUNCTION() void HandleBeverageCategoryClicked();
	UFUNCTION() void HandleFreshFoodCategoryClicked();
	UFUNCTION() void HandleFrozenFoodCategoryClicked();
	UFUNCTION() void HandlePotionCategoryClicked();
	UFUNCTION() void HandleScrollCategoryClicked();
	UFUNCTION() void HandleSnackCategoryClicked();
	UFUNCTION() void HandleSupplyCategoryClicked();
	UFUNCTION() void HandleToiletryCategoryClicked();
	UFUNCTION() void HandleToolCategoryClicked();
	UFUNCTION() void HandleProductRowSelected(FDataTableRowHandle ProductRow);
};
