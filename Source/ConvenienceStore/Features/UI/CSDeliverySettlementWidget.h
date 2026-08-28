#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "CSDeliverySettlementWidget.generated.h"

class ACSStorePlayerController;

// 배달 완료 또는 실패 결과를 확인 버튼과 함께 표시하는 전용 정산 위젯입니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSDeliverySettlementWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 배달 처리 결과와 성공 보상 정보를 전달한 뒤 블루프린트 화면을 갱신합니다.
	void InitializeDeliverySettlement(
		ACSStorePlayerController* InStorePlayerController,
		const FText& InMessage,
		bool bInSucceeded,
		ECSDeliverySettlementResult InResult,
		int32 InRewardGold,
		int32 InReputationReward);

	// 확인 버튼에서 호출하여 정산 창과 게임 입력 잠금을 함께 해제합니다.
	UFUNCTION(BlueprintCallable, Category = "Delivery Settlement")
	void CloseSettlementWindow();

	// WBP에서 성공·실패 색상과 보상 영역 표시를 구성합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Settlement")
	void RefreshDeliverySettlementView(
		const FText& Message,
		bool bSucceeded,
		ECSDeliverySettlementResult Result,
		int32 RewardGold,
		int32 ReputationReward);

protected:
	virtual void NativeDestruct() override;

private:
	TObjectPtr<ACSStorePlayerController> StorePlayerController;
};
