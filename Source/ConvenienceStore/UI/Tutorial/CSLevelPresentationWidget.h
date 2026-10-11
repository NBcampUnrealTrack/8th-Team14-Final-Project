#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSLevelStoryData.h"
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
	void Present(ECSLevelPresentationPhase Phase, ECSStorySpeaker SpeakerType, const FText& Heading, const FText& Body,
		const FText& Speaker, const FText& Caption, UTexture2D* Illustration,
		UTexture2D* SpeakerPortrait, bool bCameraWaiting = false);
	void SetLevelUpDetails(int32 PreviousLevel, int32 NewLevel, const FText& PreviousName, const FText& NewName);
	// 해금 카메라 장면마다 왼쪽 안내 패널의 입장/퇴장 연출을 새로 시작합니다.
	void BeginUnlockShot(float ShotDuration);
	// 직접 만드신 버튼의 클릭 이벤트에서도 호출하실 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "Level Presentation") void Continue();
	UFUNCTION(BlueprintCallable, Category = "Level Presentation") void SkipScene();
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") ECSLevelPresentationPhase CurrentPhase;
	// Other/Player/PlayerThought/Narration에 따라 WBP에서 화자명, 일러스트, 글자색을 구분해 주세요.
	// PlayerThought와 Narration은 같은 레이아웃을 사용하고 글자색만 다르게 구성할 수 있습니다.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") ECSStorySpeaker DisplaySpeakerType = ECSStorySpeaker::Narration;
	// 카메라 이동이 끝나면 참으로 바뀝니다. 직접 제작하신 다음 버튼 표시에도 사용해 주세요.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") bool bAwaitingCameraContinue = false;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplayHeading;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplayBody;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplaySpeaker;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") FText DisplayCaption;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") TObjectPtr<UTexture2D> DisplayIllustration;
	// 스토리 장면 전환용 전체 화면 암전입니다. WBP 구성과 무관하게 최상단에 그려집니다.
	void SetStoryFadeOpacity(float Opacity);
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Level Presentation") float StoryFadeOpacity = 0.0f;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	UFUNCTION(BlueprintImplementableEvent, Category = "Level Presentation") void OnPresentationChanged();
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> LevelUpPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> StoryPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> UnlockPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> GoalPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> IllustrationImage;
	// 대화창 레이아웃과 독립적으로 Root Canvas 어디에 두어도 이름으로 연결됩니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> SpeakerPortraitImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> TopLetterbox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> BottomLetterbox;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Cinematic",
		meta = (ClampMin = "0.05", Units = "s"))
	float LetterboxAnimationDuration = 0.45f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Cinematic",
		meta = (ClampMin = "0.05", Units = "s"))
	float PortraitFadeDuration = 0.3f;
	// 레벨업 패널이 작게 시작해 살짝 크게 튄 뒤 제 크기로 안착하는 시간입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Level Up",
		meta = (ClampMin = "0.1", Units = "s"))
	float LevelUpPopDuration = 0.38f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Level Up",
		meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float LevelUpPopStartScale = 0.72f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Level Up",
		meta = (ClampMin = "1.0", ClampMax = "1.3"))
	float LevelUpPopOvershootScale = 1.08f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Unlock",
		meta = (ClampMin = "0.05", Units = "s"))
	float UnlockPanelSlideInDuration = 0.4f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Unlock",
		meta = (ClampMin = "0.05", Units = "s"))
	float UnlockPanelSlideOutDuration = 0.35f;
	// 패널 실제 폭을 아직 계산하지 못한 첫 프레임에 사용하는 왼쪽 이동 거리입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Unlock",
		meta = (ClampMin = "0.0"))
	float UnlockPanelFallbackSlideDistance = 650.0f;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LevelUpHeadingText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LevelUpBodyText;
	// 이미지와 같은 전환형 레벨업 UI를 만들 때 아래 이름 그대로 TextBlock을 배치해 주세요.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PreviousLevelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NewLevelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PreviousStoreNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NewStoreNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DialogueText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SpeakerText;
	// 화자 종류에 따라 자동 전환되는 대사창 배경입니다. WBP의 위젯 이름과 정확히 맞춰 주세요.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> DialogueBackground_Other;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> DialogueBackground_Player;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> DialogueBackground_Inner;
	// PlayerThought와 Narration은 같은 Inner 배경을 사용하고 글자색만 구분합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Dialogue Style")
	FLinearColor OtherDialogueTextColor = FLinearColor(1.0f, 0.96f, 0.86f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Dialogue Style")
	FLinearColor PlayerDialogueTextColor = FLinearColor(1.0f, 0.96f, 0.86f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Dialogue Style")
	FLinearColor PlayerThoughtTextColor = FLinearColor(0.72f, 0.88f, 1.0f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Level Presentation|Dialogue Style")
	FLinearColor NarrationTextColor = FLinearColor(1.0f, 0.88f, 0.54f, 1.0f);
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

private:
	FReply HandlePresentationKeyDown(const FKeyEvent& Event);
	void SetCinematicModeActive(bool bActive);
	void SetSpeakerPortrait(UTexture2D* Portrait);
	void UpdateCinematicVisuals();
	bool bCinematicModeActive = false;
	float LetterboxProgress = 0.0f;
	float PortraitOpacity = 0.0f;
	double LastCinematicRealTime = 0.0;
	float LevelUpPopElapsed = 0.0f;
	bool bLevelUpPopPlaying = false;
	bool bHasPresented = false;
	float UnlockShotElapsed = 0.0f;
	float UnlockShotDuration = 0.0f;
	bool bUnlockShotPlaying = false;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> ActiveSpeakerPortrait;
};
