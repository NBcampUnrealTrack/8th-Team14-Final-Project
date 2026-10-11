#pragma once

#include "CoreMinimal.h"
#include "UI/Store/CSWakeUpWidget.h"
#include "CSNewGameOpeningWidget.generated.h"

class UAudioComponent;
class UImage;
class USoundBase;
class UTexture2D;

// 새 게임에서만 사용하는 프롤로그입니다. 일상 기상의 WBP_WakeUpWidget과 분리하여 설정해 주세요.
UCLASS()
class CONVENIENCESTORE_API UCSNewGameOpeningWidget : public UCSWakeUpWidget
{
	GENERATED_BODY()

public:
	UCSNewGameOpeningWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// WBP에서는 같은 이름의 Image를 전체 화면으로 배치해 주세요. 없으면 C++ 기본 Image를 생성합니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> OpeningIllustrationImage;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Assets") TObjectPtr<UTexture2D> OpeningIllustration;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Assets") TObjectPtr<USoundBase> FirstOpeningAudio;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Assets") TObjectPtr<USoundBase> SecondOpeningAudio;
	// 두 프롤로그 음성에만 적용되는 볼륨 배수입니다. WBP에서 음원에 맞춰 조절할 수 있습니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Audio",
		meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0"))
	float OpeningAudioVolumeMultiplier = 2.0f;
	// 눈뜨기 구간의 곡선과 눈꺼풀 색은 이 위젯의 클래스 기본값을 그대로 사용합니다.
	// 기본값은 /Game/ConvenienceStore/UI/WakeUp/WBP_WakeUpWidget 입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Eye Opening")
	TSubclassOf<UCSWakeUpWidget> EyeAppearanceTemplateClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Timing", meta = (ClampMin = "0.05", Units = "s")) float InitialFadeOutDuration = 0.5f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Timing", meta = (ClampMin = "0.05", Units = "s")) float IllustrationFadeInDuration = 0.75f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Timing", meta = (ClampMin = "0.0", Units = "s")) float FirstAudioDelay = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Timing", meta = (ClampMin = "0.0", Units = "s")) float FirstAudioDuration = 8.3f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Timing", meta = (ClampMin = "0.0", Units = "s")) float SecondAudioDuration = 6.7f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Opening|Timing", meta = (ClampMin = "0.05", Units = "s")) float FinalFadeOutDuration = 0.75f;

private:
	enum class EOpeningStage : uint8 { InitialFadeOut, IllustrationFadeIn, BeforeFirstAudio, FirstAudio, SecondAudio, EyeOpening };
	EOpeningStage OpeningStage = EOpeningStage::InitialFadeOut;
	float StageElapsed = 0.0f;
	double LastOpeningRealTime = 0.0;
	float BlackOpacity = 0.0f;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> ActiveAudioComponent;

	void AdvanceStage(EOpeningStage NewStage);
	void PlayOpeningAudio(USoundBase* Sound);
	void StopOpeningAudio();
	void ApplyEyeAppearanceTemplate();
};
