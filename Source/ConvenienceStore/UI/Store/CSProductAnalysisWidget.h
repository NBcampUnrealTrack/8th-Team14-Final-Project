#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSProductAnalysisWidget.generated.h"

class UButton;
class UImage;
class UScrollBox;
class UTextBlock;
class UWidget;
class UCSProductAnalysisRowWidget;

UCLASS(Abstract)
class CONVENIENCESTORE_API UCSProductAnalysisWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void OpenAnalysis();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> AnalysisDayText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PopularProductNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PopularProductSalesText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> OutOfStockProductNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> OutOfStockCountText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DisposedProductNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DisposedQuantityText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DisposedTypeCountText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> AllFilterButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> OutOfStockFilterButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> HasWasteFilterButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SalesSortButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> AnalysisCloseButton;

	// 필터 버튼 선택 연출용입니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> AllFilterSelectedBackground;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> AllFilterDefaultBackground;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> OutOfStockFilterSelectedBackground;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> OutOfStockFilterDefaultBackground;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> HasWasteFilterSelectedBackground;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> HasWasteFilterDefaultBackground;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SalesSortSelectedBackground;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SalesSortDefaultBackground;
	// 정렬 방향 아이콘 연출용입니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> SalesSortDirectionIcon;

	// 버튼 선택 상태의 글자색 연출용입니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> AllFilterText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> OutOfStockFilterText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HasWasteFilterText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SalesSortText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UScrollBox> ProductListScrollBox;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Product Analysis|Rows")
	TSubclassOf<UCSProductAnalysisRowWidget> ProductAnalysisRowClass;

	// 버튼 글자색은 WBP Class Defaults에서 시안에 맞게 조절합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Product Analysis|Style")
	FSlateColor BasicButtonTextColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Product Analysis|Style")
	FSlateColor SelectedButtonTextColor = FSlateColor(FLinearColor(1.0f, 0.85f, 0.35f, 1.0f));

private:
	enum class EProductFilter : uint8
	{
		All,
		OutOfStock,
		HasWaste
	};

	struct FProductAnalysisEntry
	{
		FName ProductId;
		FText ProductName;
		int32 SoldQuantity = 0;
		int32 RemainingStock = 0;
		int32 DisposedQuantity = 0;
	};

	EProductFilter ActiveFilter = EProductFilter::All;
	bool bSalesSortActive = false;
	bool bSalesSortDescending = true;
	TArray<FProductAnalysisEntry> AnalysisEntries;

	void RefreshAnalysis();
	void BuildAnalysisEntries();
	void RefreshSummaryCards();
	void RebuildRows();
	void RefreshControlPresentation();
	static FText MakeRepresentativeName(const FText& Name, int32 AdditionalTypeCount);

	UFUNCTION() void HandleAllFilterClicked();
	UFUNCTION() void HandleOutOfStockFilterClicked();
	UFUNCTION() void HandleHasWasteFilterClicked();
	UFUNCTION() void HandleSalesSortClicked();
	UFUNCTION() void HandleCloseClicked();
};
