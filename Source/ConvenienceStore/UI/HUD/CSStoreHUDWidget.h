#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSStoreHUDWidget.generated.h"

class ACSStoreGameState;
class UButton;
class UTextBlock;
class UWidget;

// 게임 상태 이벤트를 UMG 위젯 표시에 연결하는 HUD 기본 클래스입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSStoreHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Store|UI")
	void ToggleClockVisibility();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PhaseNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RemainingTimeText;

	// T 키로 표시와 숨김을 전환하는 시계 패널입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ClockPanel;

	// AM/PM 형식의 현재 게임 시각을 표시합니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ClockText;

	// 자정 이후 마감 업무가 활성화되었을 때만 표시합니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> EndOfDayPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EndOfDayTaskText;

	// 현재 업무를 완료하거나 다음 날을 시작하는 버튼입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CompleteEndOfDayTaskButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CompleteEndOfDayTaskButtonText;

	// 오늘 결제 완료된 거래의 총매출을 표시합니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DailySalesText;

	// 오늘 판매로 발생한 이익을 표시합니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DailyProfitText;

	// 오늘 결제 완료된 상품의 총수량을 표시합니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DailySoldQuantityText;

	UFUNCTION()
	void HandlePhaseChanged(ECStorePhase NewPhase);

	UFUNCTION()
	void HandleRemainingTimeChanged(float NewRemainingSeconds);

	UFUNCTION()
	void HandleGameTimeChanged(float NewGameMinutes);

	UFUNCTION()
	void HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask);

	// 결제 거래가 기록되거나 다음 날이 시작될 때 일일 판매 표시를 갱신합니다.
	UFUNCTION()
	void HandleDailySalesChanged();

	UFUNCTION()
	void CompleteEndOfDayTask();

private:
	FText FormatTime(float Seconds) const;
	FText FormatGameClock(float GameMinutes) const;
	FText GetEndOfDayButtonText(ECEndOfDayTask Task) const;
	void UpdateEndOfDayPanel();
	void UpdateDailySalesDisplay();

	TObjectPtr<ACSStoreGameState> StoreGameState;
	bool bIsClockVisible = false;
};
