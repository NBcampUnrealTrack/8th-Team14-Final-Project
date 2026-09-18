#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCombinedOrderTypes.h"
#include "Styling/SlateColor.h"
#include "TimerManager.h"
#include "CSWorldOrderWidget.generated.h"

class UCSOrderWorldWidgetComponent;
class UCSProductVisualRegistry;
class UTexture2D;

/** Native order state and controls; Blueprint owns layout and optional visual effects. */
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSWorldOrderWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCSWorldOrderWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category="Order|View")
	void RefreshNativeOrderScreen();

	UFUNCTION(BlueprintCallable, Category="Order|View")
	void RequestNativeOrderClose();

	/** Styling only: use this to override tab colors/backgrounds. */
	UFUNCTION(BlueprintImplementableEvent, Category="Order|Presentation")
	void OnOrderTabVisualsChanged(bool bFacility);

	/** Optional custom toast. C++ owns its lifetime; no cart logic belongs here. */
	UFUNCTION(BlueprintImplementableEvent, Category="Order|Presentation")
	void OnOrderNoticeChanged(const FText& Message, bool bSuccess, bool bVisible);

	UFUNCTION(BlueprintImplementableEvent, Category="Order|Presentation")
	void OnOrderAffordabilityChanged(bool bCanOrder);

	/** Optional visual override. Play Anim_OpenOrder so its completion drives native cleanup. */
	UFUNCTION(BlueprintNativeEvent, Category="Order|Presentation")
	void PlayOrderTransition(bool bReverse);
	virtual void PlayOrderTransition_Implementation(bool bReverse);

	UFUNCTION(BlueprintCallable, Category="Order|Cart")
	void SetNativeOrderQuantity(const FDataTableRowHandle& Row, bool bFacility, int32 Quantity);

	UFUNCTION(BlueprintPure, Category="Order|Cart")
	int32 GetNativeOrderQuantity(const FDataTableRowHandle& Row, bool bFacility) const;

	UFUNCTION(BlueprintCallable, Category="Order|Cart")
	void ResetNativeOrderCart();

	UFUNCTION(BlueprintPure, Category="Order|Cart")
	FCSCombinedOrderResult EvaluateNativeOrderCart(ECSOrderDeliveryType DeliveryType) const;

	/** Clears both carts only on success. Never closes the UI. */
	UFUNCTION(BlueprintCallable, Category="Order|Cart")
	FCSCombinedOrderResult SubmitNativeOrderCart(ECSOrderDeliveryType DeliveryType);

	void OpenInWorld(UCSOrderWorldWidgetComponent* InHost);
	void CompleteCloseAfterCameraReturn();

	/** Legacy close entry retained for the existing WBP wrapper. Cleanup is native. */
	UFUNCTION(BlueprintCallable, Category="Order|World UI")
	bool DeferCloseForWorldAnimation();

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(EditDefaultsOnly, Category="Order|Data")
	TObjectPtr<UDataTable> NativeProductTable;
	UPROPERTY(EditDefaultsOnly, Category="Order|Data")
	TObjectPtr<UDataTable> NativeFacilityTable;
	UPROPERTY(EditDefaultsOnly, Category="Order|Data")
	TObjectPtr<UCSProductVisualRegistry> NativeVisualRegistry;
	UPROPERTY(EditDefaultsOnly, Category="Order|Data")
	TSubclassOf<UUserWidget> NativeProductRowClass;
	UPROPERTY(EditDefaultsOnly, Category="Order|Data")
	TSubclassOf<UUserWidget> NativeFacilityRowClass;
	UPROPERTY(EditDefaultsOnly, Category="Order|Data")
	TMap<FName, TObjectPtr<UTexture2D>> NativeFacilityIcons;
	UPROPERTY(EditDefaultsOnly, Category="Order|Data", meta=(ClampMin="1"))
	int32 NativeMaxQuantity = 10;
	UPROPERTY(EditDefaultsOnly, Category="Order|Presentation", meta=(ClampMin="0.1"))
	float NativeNoticeDuration = 3.0f;

	virtual void OnAnimationFinished_Implementation(const UWidgetAnimation* Animation) override;
	virtual void NativeDestruct() override;

	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> Anim_OpenOrder;

private:
	UFUNCTION() void NativeProductTabClicked();
	UFUNCTION() void NativeFacilityTabClicked();
	UFUNCTION() void NativePlusClicked();
	UFUNCTION() void NativeMinusClicked();
	UFUNCTION() void NativeOrderClicked();
	UFUNCTION() void NativeNextDayChanged(bool bChecked);
	UFUNCTION() void NativeImmediateChanged(bool bChecked);
	UFUNCTION() void NativeProductSelected(FDataTableRowHandle ProductRow);
	UFUNCTION() void NativeFacilitySelected(FDataTableRowHandle FacilityRow);
	UFUNCTION() void NativeFundsChanged(int32 NewFunds);
	void RefreshNativeDetails();
	void RefreshNativeTotals();
	void RefreshNativeRowStates();
	void ChangeNativeSelectedQuantity(int32 Delta);
	void SetNativeDelivery(ECSOrderDeliveryType Delivery);
	void HideNativeNotice();
	void ShowNativeNotice(const FCSCombinedOrderResult& Result);
	void SetNativeText(FName Name, const FText& Text);
	UTexture2D* GetNativeIcon(const FDataTableRowHandle& Row, bool bFacility) const;
	UPROPERTY(Transient) TArray<TObjectPtr<UUserWidget>> NativeVisibleRows;
	UPROPERTY(Transient) TArray<FDataTableRowHandle> NativeVisibleHandles;
	UPROPERTY(Transient) FDataTableRowHandle NativeSelectedProduct;
	UPROPERTY(Transient) FDataTableRowHandle NativeSelectedFacility;
	UPROPERTY(Transient) TObjectPtr<class ACSStoreGameState> NativeObservedState;
	bool bNativeFacilityTab = false;
	bool bUpdatingDelivery = false;
	bool bRefreshingNativeView = false;
	ECSOrderDeliveryType NativeDelivery = ECSOrderDeliveryType::NextDay;
	FTimerHandle NativeNoticeTimer;
	FText NativeDefaultOrderLabel;
	FSlateColor NativeDefaultOrderColor;
	bool bNativeDefaultStyleCaptured = false;
	UPROPERTY(Transient)
	TArray<FCSOrderLine> NativeProductLines;
	UPROPERTY(Transient)
	TArray<FCSFacilityOrderLine> NativeFacilityLines;
	bool bSubmittingNativeOrder = false;

	UPROPERTY(Transient)
	TObjectPtr<UCSOrderWorldWidgetComponent> WorldHost;
	bool bOpening = false;
	bool bClosing = false;
	bool bCompletingClose = false;
};
