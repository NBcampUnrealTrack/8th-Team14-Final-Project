#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "CSNoticeBoardDeliveryTabWidget.generated.h"

class UCSDeliveryOrderSubsystem;
class UButton;
class UPanelWidget;
class UTextBlock;
class UWidget;

/**
 * 게시판의 배달 주문 탭입니다.
 * 주문 생성·예약·만료 처리는 소유하지 않고 UCSDeliveryOrderSubsystem에 요청만 전달합니다.
 */
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSNoticeBoardDeliveryTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 주문 목록과 요약 수치를 다시 조회해 블루프린트 화면에 전달합니다. */
	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	void RefreshDeliveryOrderList();

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	TArray<FCSDeliveryOrderRuntime> GetDeliveryOrders() const;

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	TArray<FCSDeliveryOrderRuntime> GetRecentDeliveryResults() const;

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	bool CanAcceptOrder(int32 OrderId, FText& OutReason) const;

	/** 주문 행의 수락 버튼에서 호출합니다. 변경 성공 여부와 안내 문구를 반환합니다. */
	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool AcceptOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool DeclineOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool CancelOrder(int32 OrderId, FText& OutMessage);

	/** 마법진에 올릴 상품을 결정할 발송 준비 주문을 선택합니다. */
	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool PrepareOrderForDispatch(int32 OrderId, FText& OutMessage);

	// 기존 WBP_DeliveryOrder의 그래프를 다시 만들지 않도록 단말기 UI와 같은 API를 제공합니다.
	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	FCSDeliveryOrderRuntime GetDisplayedOrder() const;

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	int32 GetTodayGeneratedOrderCount() const;

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	int32 GetDailyOrderLimit() const;

	UFUNCTION(BlueprintPure, Category = "Notice Board|Delivery")
	int32 GetAcceptedOrderCount() const;

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool AcceptDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool DeclineDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery")
	bool CancelActiveOrder(FText& OutMessage);

	/** 기존 WBP_DeliveryOrder의 닫기 버튼을 게시판 전체 창 닫기로 연결합니다. */
	UFUNCTION(BlueprintCallable, Category = "Notice Board|Delivery", meta = (DisplayName = "Close Delivery Order Window"))
	void CloseOrderWindow();

	/** ScrollBox의 주문 행을 구성하고 빈 상태·요약 텍스트를 갱신하는 BP 이벤트입니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Delivery")
	void RefreshDeliveryOrderListView(
		const TArray<FCSDeliveryOrderRuntime>& Orders,
		int32 TodayGeneratedOrderCount,
		int32 AcceptedOrderCount);

	/** 기한 초과 등 이미 끝난 최근 주문을 게시판 기록 영역에 전달합니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Delivery")
	void RefreshRecentDeliveryResults(const TArray<FCSDeliveryOrderRuntime>& RecentResults);

	/** 기존 주문 UI의 선택 주문 상세 표시 이벤트와 호환됩니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Delivery")
	void RefreshDeliveryOrderView(
		const FCSDeliveryOrderRuntime& Order,
		bool bCanAccept,
		const FText& AvailabilityMessage);

	/** 수락 버튼 결과를 토스트 등으로 표시하는 BP 이벤트입니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Delivery")
	void ShowDeliveryOrderResult(bool bSucceeded, const FText& Message);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	UFUNCTION()
	void HandleDeliveryOrdersChanged();

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderSubsystem> DeliveryOrderSubsystem;

	/** 새 게시판 디자인의 요청 상품 한 줄 WBP입니다. 비우면 기본 경로를 자동 사용합니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Delivery|Style")
	TSubclassOf<UUserWidget> RequestItemRowClass;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Button_Accept;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Button_Decline;

private:
	void RefreshSingleOrderPresentation(
		const FCSDeliveryOrderRuntime& Order,
		bool bCanAccept,
		const FText& AvailabilityMessage);
	void PopulateRequestItemRows(const FCSDeliveryOrderRuntime& Order);
	void SetupRequestItemRow(
		UUserWidget* RowWidget,
		const FCSDeliveryOrderLine& Line,
		int32 AvailableQuantity) const;
	UPanelWidget* ResolveRequestItemPanel() const;
	TSubclassOf<UUserWidget> ResolveRequestItemRowClass() const;
	FText FormatGameClock(float GameMinutes) const;
	void SetNamedText(FName WidgetName, const FText& Text) const;
	void SetNamedVisibility(FName WidgetName, ESlateVisibility InVisibility) const;

	UFUNCTION() void HandleAcceptClicked();
	UFUNCTION() void HandleDeclineClicked();
};
