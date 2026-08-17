#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSDailySummaryWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
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

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BusinessDayResultText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> MainNetProfitText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> NetProfitDeltaIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NetProfitDeltaText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BreakdownSalesText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BreakdownTotalLossText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BreakdownNetProfitText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailySalesText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailyWasteLossText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailySoldQuantityText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ReputationScoreText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> ReputationDeltaIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ReputationDeltaText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ProductAnalysisButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCSProductAnalysisWidget> ProductAnalysisPanel;

	// 0을 포함한 상승 상태의 색상은 WBP Class Defaults에서 시안에 맞게 조절합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Summary|Style")
	FLinearColor NonNegativeColor = FLinearColor(0.35f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Summary|Style")
	FLinearColor NegativeColor = FLinearColor(0.9f, 0.2f, 0.2f, 1.0f);

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> StoreGameState;

	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleProductAnalysisClicked();
	UFUNCTION() void HandleDailySummaryDataChanged();
	UFUNCTION() void HandleReputationChanged(int32 NewReputationScore);
	void ApplyDeltaPresentation(
		int32 Delta,
		UTextBlock* DeltaText,
		UImage* DeltaIcon,
		const FText& NonNegativeFormat,
		const FText& NegativeFormat);
};
