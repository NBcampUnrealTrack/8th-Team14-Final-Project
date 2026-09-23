#include "UI/Store/CSStoreClosingBellConfirmWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
void UCSStoreClosingBellConfirmWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (IsValid(MessageText)) MessageText->SetText(NSLOCTEXT("Store", "ConfirmSleep", "오늘은 이만 잠들까요?"));
	if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.AddDynamic(this, &UCSStoreClosingBellConfirmWidget::HandleConfirmClicked);
	if (IsValid(CancelButton)) CancelButton->OnClicked.AddDynamic(this, &UCSStoreClosingBellConfirmWidget::HandleCancelClicked);
}
void UCSStoreClosingBellConfirmWidget::HandleConfirmClicked() { OnClosingBellConfirmed.Broadcast(); }
void UCSStoreClosingBellConfirmWidget::HandleCancelClicked() { OnClosingBellCancelled.Broadcast(); }
