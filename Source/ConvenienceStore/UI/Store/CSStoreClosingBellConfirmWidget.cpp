#include "UI/Store/CSStoreClosingBellConfirmWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
void UCSStoreClosingBellConfirmWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsValid(MessageText)) MessageText->SetText(NSLOCTEXT("Store", "ConfirmNextDay", "마감을 종료하고 다음 날을 시작하시겠습니까?"));
	if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.AddDynamic(this, &UCSStoreClosingBellConfirmWidget::HandleConfirmClicked);
	if (IsValid(CancelButton)) CancelButton->OnClicked.AddDynamic(this, &UCSStoreClosingBellConfirmWidget::HandleCancelClicked);
}
void UCSStoreClosingBellConfirmWidget::HandleConfirmClicked() { OnClosingBellConfirmed.Broadcast(); }
void UCSStoreClosingBellConfirmWidget::HandleCancelClicked() { OnClosingBellCancelled.Broadcast(); }
