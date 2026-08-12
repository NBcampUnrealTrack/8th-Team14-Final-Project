#include "UI/Store/CSStoreBusinessConfirmWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UCSStoreBusinessConfirmWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(ConfirmButton))
	{
		ConfirmButton->OnClicked.AddDynamic(this, &UCSStoreBusinessConfirmWidget::HandleConfirmClicked);
	}
	if (IsValid(CancelButton))
	{
		CancelButton->OnClicked.AddDynamic(this, &UCSStoreBusinessConfirmWidget::HandleCancelClicked);
	}

	SetConfirmationType(bShouldOpen);
}

void UCSStoreBusinessConfirmWidget::SetConfirmationType(bool bInShouldOpen)
{
	bShouldOpen = bInShouldOpen;
	if (IsValid(MessageText))
	{
		MessageText->SetText(bShouldOpen
			? NSLOCTEXT("Store", "ConfirmOpenStore", "영업을 시작하시겠습니까?")
			: NSLOCTEXT("Store", "ConfirmCloseStore", "영업을 종료하시겠습니까?\n현재 매장에 있는 고객의 계산은 계속 진행할 수 있습니다."));
	}
}

void UCSStoreBusinessConfirmWidget::HandleConfirmClicked()
{
	OnBusinessConfirmed.Broadcast(bShouldOpen);
}

void UCSStoreBusinessConfirmWidget::HandleCancelClicked()
{
	OnBusinessCancelled.Broadcast();
}
