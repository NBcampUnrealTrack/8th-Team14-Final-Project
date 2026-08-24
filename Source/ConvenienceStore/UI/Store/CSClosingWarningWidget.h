#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSClosingWarningWidget.generated.h"

class UTextBlock;
class UWidgetAnimation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnClosingWarningFinished);

// 마감 전 남은 시간을 상단 중앙에 한 번 표시하는 독립 알림 위젯입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSClosingWarningWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowWarning(int32 RemainingMinutes);

	UPROPERTY(BlueprintAssignable, Category = "Store|Closing Warning")
	FCSOnClosingWarningFinished OnWarningFinished;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> WarningText;

	UPROPERTY(Transient, meta = (BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> WarningAnimation;

private:
	UFUNCTION()
	void HandleWarningAnimationFinished();

	bool bIsWarningPlaying = false;
};
