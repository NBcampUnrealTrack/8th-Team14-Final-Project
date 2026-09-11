#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSWorldOrderWidget.generated.h"

class UCSOrderWorldWidgetComponent;

/** Presentation only; the existing Blueprint continues to own the order cart. */
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSWorldOrderWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void OpenInWorld(UCSOrderWorldWidgetComponent* InHost);
	void CompleteCloseAfterCameraReturn();

	/** Defer Blueprint cleanup until both the reverse animation and camera return finish. */
	UFUNCTION(BlueprintCallable, Category="Order|World UI")
	bool DeferCloseForWorldAnimation();

	UFUNCTION(BlueprintImplementableEvent, Category="Order|World UI")
	void CompleteWorldOrderClose();

protected:
	virtual void OnAnimationFinished_Implementation(const UWidgetAnimation* Animation) override;
	virtual void NativeDestruct() override;

	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> Anim_OpenOrder;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCSOrderWorldWidgetComponent> WorldHost;
	bool bOpening = false;
	bool bClosing = false;
	bool bCompletingClose = false;
};
