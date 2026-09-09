#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSDeliveryToastWidget.generated.h"

class UTextBlock;
class UWidgetAnimation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnDeliveryToastFinished);

/** 입력을 막지 않고 화면 한쪽에 잠깐 표시하는 배달 시스템 알림입니다. */
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSDeliveryToastWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowToast(const FText& InTitle, const FText& InMessage);

	UPROPERTY(BlueprintAssignable, Category = "Delivery|Toast")
	FCSOnDeliveryToastFinished OnToastFinished;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Title;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Message;

	/** WBP에서 같은 이름으로 만든 등장·대기·퇴장 애니메이션입니다. */
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> ToastAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Toast", meta = (ClampMin = "0.1"))
	float FallbackDurationSeconds = 3.0f;

private:
	UFUNCTION()
	void HandleToastAnimationFinished();

	void FinishToast();
	FTimerHandle FallbackTimerHandle;
	bool bIsToastPlaying = false;
};
