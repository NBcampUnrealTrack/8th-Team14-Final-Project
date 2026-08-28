#include "Features/UI/CSDeliverySettlementWidget.h"

#include "Systems/StoreFlow/CSStorePlayerController.h"

void UCSDeliverySettlementWidget::InitializeDeliverySettlement(
	ACSStorePlayerController* InStorePlayerController,
	const FText& InMessage,
	const bool bInSucceeded,
	const ECSDeliverySettlementResult InResult,
	const int32 InRewardGold,
	const int32 InReputationReward)
{
	StorePlayerController = InStorePlayerController;
	RefreshDeliverySettlementView(
		InMessage,
		bInSucceeded,
		InResult,
		InRewardGold,
		InReputationReward);
}

void UCSDeliverySettlementWidget::CloseSettlementWindow()
{
	if (IsValid(StorePlayerController))
	{
		StorePlayerController->SetGameplayUIInputLock(this, false);
	}
	RemoveFromParent();
}

void UCSDeliverySettlementWidget::NativeDestruct()
{
	if (IsValid(StorePlayerController))
	{
		// 외부에서 위젯이 제거되어도 플레이어 입력이 잠긴 채 남지 않도록 해제합니다.
		StorePlayerController->SetGameplayUIInputLock(this, false);
	}
	Super::NativeDestruct();
}
