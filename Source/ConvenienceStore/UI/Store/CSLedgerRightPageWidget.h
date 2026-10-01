#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSLedgerTypes.h"
#include "CSLedgerRightPageWidget.generated.h"

class UButton;
class UImage;
class USpinBox;
class UTextBlock;
class UWidgetSwitcher;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnLedgerSalePriceEdited, int32, NewSalePrice);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnLedgerRightPageCloseRequested);

UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSLedgerRightPageWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowEmpty();
	void SetFunds(int32 CurrentFunds);
	void ShowUnlocked(const FCSLedgerProductViewData& ProductData, int32 PendingSalePrice);
	void ShowLocked(const FCSLedgerProductViewData& ProductData);
	void SetPendingSalePriceValue(int32 NewSalePrice);

	UPROPERTY(BlueprintAssignable, Category = "Store|Ledger")
	FCSOnLedgerSalePriceEdited OnSalePriceEdited;

	UPROPERTY(BlueprintAssignable, Category = "Store|Ledger")
	FCSOnLedgerRightPageCloseRequested OnCloseRequested;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> DecreaseSalePriceButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> IncreaseSalePriceButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USpinBox> SalePriceSpinBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidgetSwitcher> SelectionSwitcher;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidgetSwitcher> ProductStatusSwitcher;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Funds;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Category;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ProductName;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_Stock;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_MarketPrice;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_PurchasePrice;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ShelfLife;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_LevelReq;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_LevelStatus;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ReputationReq;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_ReputationStatus;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> Img_Product;

	// 아래 잠금 관련 바인딩/이벤트/색상은 기존 WBP 직렬화 호환용으로만 보존합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Locked")
	FLinearColor MetConditionColor = FLinearColor(0.25f, 0.45f, 0.12f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Ledger|Locked")
	FLinearColor UnmetConditionColor = FLinearColor(0.55f, 0.12f, 0.08f, 1.0f);

	// 스위처 전환은 C++가 담당하고 각 상태 안의 텍스트·아이콘 표현은 WBP가 담당합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Store|Ledger", meta = (DisplayName = "Show Ledger Unlocked Product Detail"))
	void ShowUnlockedProductDetail(const FCSLedgerProductViewData& ProductData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Store|Ledger", meta = (DisplayName = "Show Ledger Locked Product Detail"))
	void ShowLockedProductDetail(const FCSLedgerProductViewData& ProductData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Store|Ledger", meta = (DisplayName = "Refresh Ledger Pending Sale Price View"))
	void RefreshPendingSalePriceView(int32 NewPendingSalePrice);

private:
	UFUNCTION() void HandleDecreaseClicked();
	UFUNCTION() void HandleIncreaseClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleSalePriceCommitted(float Value, ETextCommit::Type CommitMethod);
	void RefreshProductHeader(const FCSLedgerProductViewData& ProductData);
	void RefreshUnlockedMetrics(const FCSLedgerProductViewData& ProductData);
};
