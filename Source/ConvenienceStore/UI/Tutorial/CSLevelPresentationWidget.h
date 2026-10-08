#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSLevelPresentationWidget.generated.h"

class UTextBlock;
class UImage;
class UButton;
class UTexture2D;

UENUM(BlueprintType)
enum class ECSLevelPresentationPhase : uint8 { LevelUp, Story, Unlock, Goal };

// 같은 이름의 위젯을 배치하시거나 표시 변경 이벤트를 구현하여 외형을 수정해 주세요.
UCLASS()
class CONVENIENCESTORE_API UCSLevelPresentationWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Present(ECSLevelPresentationPhase Phase, const FText& Heading, const FText& Body,
		const FText& Speaker, const FText& Caption, UTexture2D* Illustration, bool bCameraWaiting = false);
	// 직접 만드신 버튼의 클릭 이벤트에서도 호출하실 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "Level Presentation") void Continue();
	UFUNCTION(BlueprintCallable, Category = "Level Presentation") void SkipScene();
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") ECSLevelPresentationPhase CurrentPhase;
	// 카메라 이동이 끝나면 참으로 바뀝니다. 직접 제작하신 다음 버튼 표시에도 사용해 주세요.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") bool bAwaitingCameraContinue = false;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplayHeading;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplayBody;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplaySpeaker;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplayCaption;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") TObjectPtr<UTexture2D> DisplayIllustration;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	UFUNCTION(BlueprintImplementableEvent, Category = "Level Presentation") void OnPresentationChanged();
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> LevelUpPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> StoryPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> UnlockPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> GoalPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> IllustrationImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LevelUpHeadingText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LevelUpBodyText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DialogueText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SpeakerText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CaptionText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> UnlockHeadingText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> UnlockBodyText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> GoalText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> StoryStartButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> DialogueNextButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> StorySkipButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CameraSkipButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CameraNextButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ResumeButton;
};
