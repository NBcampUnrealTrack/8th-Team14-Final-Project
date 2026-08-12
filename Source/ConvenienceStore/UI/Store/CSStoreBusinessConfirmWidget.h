#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSStoreBusinessConfirmWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnStoreBusinessConfirmed, bool, bShouldOpen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnStoreBusinessConfirmCancelled);

// 입간판의 영업 시작·종료를 한 번 더 확인하는 공통 확인 창입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSStoreBusinessConfirmWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetConfirmationType(bool bInShouldOpen);

	UPROPERTY(BlueprintAssignable, Category = "Store|Business")
	FCSOnStoreBusinessConfirmed OnBusinessConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "Store|Business")
	FCSOnStoreBusinessConfirmCancelled OnBusinessCancelled;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConfirmButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	UFUNCTION()
	void HandleConfirmClicked();

	UFUNCTION()
	void HandleCancelClicked();

	bool bShouldOpen = false;
};
