#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSDailySummaryWidget.generated.h"

class UButton;
class UTextBlock;

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
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailySalesText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailyProfitText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailySoldQuantityText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ConfirmButton;
	UFUNCTION() void HandleConfirmClicked();
};
