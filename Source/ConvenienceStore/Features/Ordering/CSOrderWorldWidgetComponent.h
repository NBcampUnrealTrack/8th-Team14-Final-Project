#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "CSOrderWorldWidgetComponent.generated.h"

class UCSWorldOrderWidget;
class UCameraComponent;
class APlayerCameraManager;
class ACSStorePlayerController;

/** Camera/2D order-menu host. Component class is retained for existing Blueprint references. */
UCLASS(ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class CONVENIENCESTORE_API UCSOrderWorldWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()
public:
	UCSOrderWorldWidgetComponent();

	UFUNCTION(BlueprintCallable, Category="Order|World UI")
	void PresentOrderWidget(UCSWorldOrderWidget* OrderWidget);

	void FinishPresentation();
	void CancelPresentation();

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category="Order|Camera", meta=(ClampMin="0.0", Units="s"))
	float CameraBlendTime = 0.35f;

private:
	enum class EPresentationState : uint8 { Closed, MovingIn, Open, MovingOut };
	EPresentationState PresentationState = EPresentationState::Closed;
	void HandleCameraBlendComplete();
	void CompleteCameraReturn();
	void RemoveCameraBlendListener();
	void RestoreCameraActivation();

	TWeakObjectPtr<ACSStorePlayerController> ViewingPlayer;
	TWeakObjectPtr<APlayerCameraManager> CameraManager;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TWeakObjectPtr<UCameraComponent> OrderCamera;
	TArray<TWeakObjectPtr<UCameraComponent>> PreviouslyActiveCameras;
	FDelegateHandle CameraBlendHandle;
	FRotator PreviousControlRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	TObjectPtr<UCSWorldOrderWidget> PresentedWidget;
};
