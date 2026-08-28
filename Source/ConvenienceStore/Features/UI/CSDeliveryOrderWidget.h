#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "CSDeliveryOrderWidget.generated.h"

class ACSDeliveryOrderTerminalActor;
class UCSDeliveryOrderSubsystem;

// 블루프린트 주문 UI가 수락·거절·취소와 주문 데이터를 공통으로 사용하도록 하는 기반 위젯입니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSDeliveryOrderWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	void InitializeDeliveryOrderTerminal(ACSDeliveryOrderTerminalActor* InOrderTerminal);

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	FCSDeliveryOrderRuntime GetDisplayedOrder() const;

	// 현재 단말기에 표시할 주문 전체와 요약 수치를 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	TArray<FCSDeliveryOrderRuntime> GetDisplayedOrders() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	int32 GetTodayGeneratedOrderCount() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	int32 GetAcceptedOrderCount() const;

	// 목록 항목 위젯을 초기화할 때 동일한 주문 단말기를 전달합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	ACSDeliveryOrderTerminalActor* GetOrderTerminal() const { return OrderTerminal; }

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool AcceptDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool DeclineDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool CancelActiveOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool AcceptOrderById(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool DeclineOrderById(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool CancelOrderById(int32 OrderId, FText& OutMessage);

	// 주문 상태가 바뀐 뒤 목록 UI를 즉시 다시 그릴 때 호출합니다.
	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	void RefreshOrderList();

	// 단말기 함수와 이름이 겹리지 않도록, 위젯 자신을 닫는 용도를 명확히 구분합니다.
	UFUNCTION(BlueprintCallable, Category = "Delivery Order", meta = (DisplayName = "Close Delivery Order Window"))
	void CloseOrderWindow();

	// 위젯을 열거나 주문 상태가 변할 때 블루프린트에서 텍스트·버튼 상태를 갱신합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Order")
	void RefreshDeliveryOrderView(const FCSDeliveryOrderRuntime& Order, bool bCanAccept, const FText& AvailabilityMessage);

	// 여러 주문을 ScrollBox와 토글 항목으로 구성하는 목록 UI용 이벤트입니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Order")
	void RefreshDeliveryOrderListView(
		const TArray<FCSDeliveryOrderRuntime>& Orders,
		int32 TodayGeneratedOrderCount,
		int32 AcceptedOrderCount);

	virtual void NativeDestruct() override;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Delivery Order")
	TObjectPtr<ACSDeliveryOrderTerminalActor> OrderTerminal;

	UFUNCTION()
	void HandleDeliveryOrdersChanged();

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderSubsystem> BoundDeliveryOrderSubsystem;
};
