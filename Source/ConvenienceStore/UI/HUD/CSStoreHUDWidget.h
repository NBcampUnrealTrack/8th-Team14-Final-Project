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

	// 조준점 아래의 상호작용 안내를 지정한 색상으로 표시합니다.
	void SetInteractionPrompt(const FText& Message, const FLinearColor& TextColor);

	// 상품 가격과 보관 경고를 숨긴 뒤, 빈 슬롯 안내만 빨간색으로 표시합니다.
	void ShowPlacementUnavailablePrompt();

	// 조준한 상품의 권장 판매가를 표시합니다.
	void SetProductPrice(const FText& Message);

	// 조준한 개별 상품의 남은 유통기한을 지정한 색상으로 표시합니다.
	void SetProductShelfLife(const FText& Message, const FLinearColor& TextColor);

	// 현재 진열 환경이 상품 보관 조건과 맞지 않을 때 노란 경고를 표시합니다.
	void SetStorageWarning(const FText& Message);

	// 조준 대상이 바뀌거나 상품을 내려놓았을 때 조준점 주변 안내를 모두 숨깁니다.
	void ClearInteractionContext();

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

	// 조준한 대상에 대해 E 또는 Q로 실행할 행동을 안내합니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InteractionPromptText;

	// 이후 상품 조준 시 판매가를 표시할 영역입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProductPriceText;

	// 개별 상품 RuntimeState의 남은 유통기한을 표시할 영역입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProductShelfLifeText;

	// 냉장·냉동 보관 경고를 표시할 영역입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StorageWarningText;

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
