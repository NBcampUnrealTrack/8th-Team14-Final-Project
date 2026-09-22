#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSMagicCircleUpgradeWidget.generated.h"

class UButton;
class UTextBlock;
class ACSStoreGameState;
class UCSDeliveryOrderSubsystem;

USTRUCT(BlueprintType)
struct FCSMagicCircleUpgradeViewData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CurrentLevel = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NextLevel = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DailyOrderLimit = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NextDailyOrderLimit = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TodayGeneratedOrderCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RequiredStoreLevel = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CurrentStoreLevel = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 UpgradeCost = 30000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CurrentFunds = 45000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 FundsAfterUpgrade = 15000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bMaximumLevel = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCanUpgrade = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Message;
};

// 자식 위젯 블루프린트가 모든 레이아웃을 소유하고 C++이 데이터와 버튼을 연결합니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSMagicCircleUpgradeWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCSMagicCircleUpgradeWidget(const FObjectInitializer& ObjectInitializer);
	UFUNCTION(BlueprintCallable, Category = "MagicCircle|Upgrade") bool ConfirmUpgrade();
	UFUNCTION(BlueprintCallable, Category = "MagicCircle|Upgrade") void RefreshView();
	UFUNCTION(BlueprintCallable, Category = "MagicCircle|Upgrade") void Close();
	UPROPERTY(BlueprintReadOnly, Category = "MagicCircle|Upgrade") FCSMagicCircleUpgradeViewData ViewData;
protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCircle|Preview") FCSMagicCircleUpgradeViewData PreviewData;
	// 이름과 타입이 일치하면 C++에서 버튼 클릭을 연결합니다.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CancelButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LevelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DailyOrderLimitText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TodayOrdersText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> UpgradeCostText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CurrentFundsText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> FundsAfterUpgradeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> AvailabilityText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ConfirmButtonText;
	UFUNCTION(BlueprintImplementableEvent, Category = "MagicCircle|Upgrade") void OnUpgradeViewUpdated();
	UFUNCTION(BlueprintImplementableEvent, Category = "MagicCircle|Upgrade") void OnUpgradeSucceeded(int32 NewLevel);
private:
	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleFundsChanged(int32 NewFunds);
	UFUNCTION() void HandleUpgraded(int32 NewLevel);
	UFUNCTION() void HandleOrdersChanged();
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> StoreState;
	UPROPERTY(Transient) TObjectPtr<UCSDeliveryOrderSubsystem> DeliverySubsystem;
	FText ActionMessage;
};
