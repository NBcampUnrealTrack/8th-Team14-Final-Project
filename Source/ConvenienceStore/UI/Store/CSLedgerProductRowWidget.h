#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSLedgerTypes.h"
#include "CSLedgerProductRowWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UWidget;

// 개별 행은 선택 사실만 알리고 실제 선택 상태와 상세 페이지는 부모 장부가 관리합니다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnLedgerProductRowSelected,
	FDataTableRowHandle,
	ProductRow);

UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSLedgerProductRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Store|Ledger")
	void SetProductData(const FCSLedgerProductViewData& InProductData, bool bInSelected);

	UFUNCTION(BlueprintCallable, Category = "Store|Ledger")
	void SetSelected(bool bInSelected);

	UPROPERTY(BlueprintAssignable, Category = "Store|Ledger")
	FCSOnLedgerProductRowSelected OnProductSelected;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ProductButton;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> Img_Product;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ProductName;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Category;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Stock;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Row;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> LockOnImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> LockedStatus;

	// 상품명과 재고는 서로 다른 기본색을 쓸 수 있어 각각의 선택 색상을 분리합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Style")
	FSlateColor DefaultNameColor = FSlateColor(FLinearColor(0.22f, 0.15f, 0.08f, 1.0f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Style")
	FSlateColor SelectedNameColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Style")
	FSlateColor DefaultStockColor = FSlateColor(FLinearColor(0.22f, 0.15f, 0.08f, 1.0f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Style")
	FSlateColor SelectedStockColor = FSlateColor(FLinearColor::White);

	// 목록 재생성이나 선택 갱신 때 동일한 표시 데이터를 다시 사용할 수 있도록 보관합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Store|Ledger")
	FCSLedgerProductViewData DisplayedProduct;

	UPROPERTY(BlueprintReadOnly, Category = "Store|Ledger")
	bool bSelected = false;

private:
	void RefreshRowView();

	UFUNCTION()
	void HandleProductButtonClicked();
};
