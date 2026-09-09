#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "CSNoticeBoardDeliveryOrderEntryWidget.generated.h"

class UCSNoticeBoardDeliveryTabWidget;

/** 게시판 배달 탭의 주문 한 행입니다. 단말기 액터 대신 부모 탭을 통해 요청합니다. */
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSNoticeBoardDeliveryOrderEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	void InitializeDeliveryOrderEntry(
		UCSNoticeBoardDeliveryTabWidget* InDeliveryTab,
		const FCSDeliveryOrderRuntime& InOrder);

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	FCSDeliveryOrderRuntime GetDisplayedOrder() const { return DisplayedOrder; }

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	bool CanAcceptDisplayedOrder(FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool AcceptDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool DeclineDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool CancelDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool PrepareDisplayedOrderForDispatch(FText& OutMessage);

	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Delivery")
	void RefreshDeliveryOrderEntryView(
		const FCSDeliveryOrderRuntime& Order,
		bool bCanAccept,
		const FText& AvailabilityMessage);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Notice Board|Delivery")
	TObjectPtr<UCSNoticeBoardDeliveryTabWidget> DeliveryTab;

	UPROPERTY(BlueprintReadOnly, Category = "Notice Board|Delivery")
	FCSDeliveryOrderRuntime DisplayedOrder;
};
