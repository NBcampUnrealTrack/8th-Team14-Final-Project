#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "CSDeliveryOrderEntryWidget.generated.h"

class ACSDeliveryOrderTerminalActor;

// 배달 주문 목록의 한 행이 자신의 주문 번호로 수락·거절·취소를 요청하도록 하는 기반 위젯입니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSDeliveryOrderEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	void InitializeDeliveryOrderEntry(
		ACSDeliveryOrderTerminalActor* InOrderTerminal,
		const FCSDeliveryOrderRuntime& InOrder);

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	FCSDeliveryOrderRuntime GetDisplayedOrder() const { return DisplayedOrder; }

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool CanAcceptDisplayedOrder(FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool AcceptDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool DeclineDisplayedOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool CancelDisplayedOrder(FText& OutMessage);

	// 각 주문 행의 텍스트·버튼·토글 상세 영역을 갱신하는 블루프린트 이벤트입니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Order")
	void RefreshDeliveryOrderEntryView(
		const FCSDeliveryOrderRuntime& Order,
		bool bCanAccept,
		const FText& AvailabilityMessage);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Delivery Order")
	TObjectPtr<ACSDeliveryOrderTerminalActor> OrderTerminal;

	UPROPERTY(BlueprintReadOnly, Category = "Delivery Order")
	FCSDeliveryOrderRuntime DisplayedOrder;
};
