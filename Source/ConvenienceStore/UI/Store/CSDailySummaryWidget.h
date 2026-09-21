#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSDailySummaryWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UWidget;
class ACSStoreGameState;
class UCSProductAnalysisWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnDailySummaryConfirmed);

// 자정 영업 종료 시 당일 통계를 확인하는 단일 팝업입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSDailySummaryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void RefreshSummary();
	UPROPERTY(BlueprintAssignable, Category = "Store|Closing")
	FCSOnDailySummaryConfirmed OnDailySummaryConfirmed;
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BusinessDayResultText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> MainNetProfitText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> NetProfitDeltaIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NetProfitDeltaText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BreakdownSalesText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BreakdownTotalLossText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BreakdownNetProfitText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailySalesText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailyWasteLossText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailyRentText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RentAmountText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RentDDayText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailySoldQuantityText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ReputationScoreText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> ReputationDeltaIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ReputationDeltaText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ProductAnalysisButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCSProductAnalysisWidget> ProductAnalysisPanel;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailyTotalExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailySeparateLossText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> TotalExpenseHoverButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SeparateLossHoverButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ExpenseBreakdownPopup;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SeparateLossBreakdownPopup;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ProductOrderExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ProductOrderExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> FacilityOrderExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> FacilityOrderExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> QuickProductOrderExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> QuickProductOrderExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> QuickFacilityOrderExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> QuickFacilityOrderExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> RentExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RentExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> EmployeeHireExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> EmployeeHireExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> EmployeeWageExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> EmployeeWageExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> MagicCircleUpgradeExpenseRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> MagicCircleUpgradeExpenseText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ExpenseDivider;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ExpenseTotalRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ExpenseTotalText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NoExpenseText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> WasteLossRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> WasteLossAmountText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ShopliftLossRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ShopliftLossAmountText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SeparateLossDivider;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SeparateLossTotalRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SeparateLossTotalText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NoSeparateLossText;

	// 0을 포함한 상승 상태의 색상은 WBP Class Defaults에서 시안에 맞게 조절합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Summary|Style")
	FLinearColor NonNegativeColor = FLinearColor(0.35f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Summary|Style")
	FLinearColor NegativeColor = FLinearColor(0.9f, 0.2f, 0.2f, 1.0f);

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> StoreGameState;

	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleProductAnalysisClicked();
	UFUNCTION() void HandleTotalExpenseHovered();
	UFUNCTION() void HandleTotalExpenseUnhovered();
	UFUNCTION() void HandleSeparateLossHovered();
	UFUNCTION() void HandleSeparateLossUnhovered();
	UFUNCTION() void HandleDailySummaryDataChanged();
	UFUNCTION() void HandleReputationChanged(int32 NewReputationScore);
	void ApplyDeltaPresentation(
		int32 Delta,
		UTextBlock* DeltaText,
		UImage* DeltaIcon,
		const FText& NonNegativeFormat,
		const FText& NegativeFormat);
};
