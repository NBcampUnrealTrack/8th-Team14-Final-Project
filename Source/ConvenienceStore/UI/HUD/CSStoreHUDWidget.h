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

	UPROPERTY(meta = (BindWidgetOptional))
	// T 키로 표시와 숨김을 전환하는 시계 패널입니다.
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

	UFUNCTION()
	void HandlePhaseChanged(ECStorePhase NewPhase);

	UFUNCTION()
	void HandleRemainingTimeChanged(float NewRemainingSeconds);

	UFUNCTION()
	void HandleGameTimeChanged(float NewGameMinutes);

	UFUNCTION()
	void HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask);

	UFUNCTION()
	void CompleteEndOfDayTask();

private:
	FText FormatTime(float Seconds) const;
	FText FormatGameClock(float GameMinutes) const;
	FText GetEndOfDayButtonText(ECEndOfDayTask Task) const;
	void UpdateEndOfDayPanel();

	TObjectPtr<ACSStoreGameState> StoreGameState;
	bool bIsClockVisible = false;
};
