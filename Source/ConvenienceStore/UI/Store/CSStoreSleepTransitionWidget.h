#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSStoreSleepTransitionWidget.generated.h"

class UButton;
class UImage;
class UWidgetAnimation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnSleepNextDayRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnSleepSummaryRequested);

// 수면 스프라이트 재생과 화면 페이드를 자체 처리하는 다음 날 전환 화면입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSStoreSleepTransitionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Store|Sleep")
	FCSOnSleepNextDayRequested OnNextDayRequested;

	UPROPERTY(BlueprintAssignable, Category = "Store|Sleep")
	FCSOnSleepSummaryRequested OnSummaryRequested;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// WBP에서 아래 이름 그대로 Image와 Button을 배치하면 별도 그래프 없이 연결됩니다.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SleepSpriteImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> NextDayButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> FinalSummaryButton;

	// WBP 애니메이션 이름을 SleepZLoop로 만들면 C++에서 무한 반복 재생합니다.
	UPROPERTY(Transient, meta = (BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> SleepZLoop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Sleep|Fade", meta = (ClampMin = "0.01"))
	float WidgetFadeDuration = 0.35f;

private:
	float FadeElapsed = 0.0f;
	bool bFadingIn = true;
	bool bFadingOut = false;
	bool bNextDayBroadcast = false;

	UFUNCTION()
	void HandleNextDayClicked();

	UFUNCTION()
	void HandleFinalSummaryClicked();
};
