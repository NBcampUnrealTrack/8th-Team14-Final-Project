#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSStoreClosingBellConfirmWidget.generated.h"

class UButton;
class UTextBlock;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnClosingBellConfirmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnClosingBellCancelled);

// 마감 종을 눌렀을 때 다음 날 시작 여부를 한 번 더 확인하는 UI입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSStoreClosingBellConfirmWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable, Category = "Store|Closing") FCSOnClosingBellConfirmed OnClosingBellConfirmed;
	UPROPERTY(BlueprintAssignable, Category = "Store|Closing") FCSOnClosingBellCancelled OnClosingBellCancelled;
protected:
	virtual void NativeConstruct() override;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CancelButton;
	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleCancelClicked();
};
