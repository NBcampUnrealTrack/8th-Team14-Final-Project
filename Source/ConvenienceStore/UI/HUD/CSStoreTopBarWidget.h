#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSStorePhaseTypes.h"
#include "TimerManager.h"
#include "CSStoreTopBarWidget.generated.h"

class ACSStoreGameState;
class UTextBlock;
class UProgressBar;
class UImage;
class USizeBox;
class UWidget;

UENUM(BlueprintType)
enum class ECSStoreTopBarStatus : uint8
{
	Preparation,
	OpenEntryAllowed,
	OpenEntryPaused,
	Closed
};

// 상단 정보 바는 플레이 HUD의 숨김 요청과 독립적으로 표시됩니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSStoreTopBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 건축 모드에서는 기존 영업 상태 배너를 새 건축 모드 배너로 교체합니다.
	void SetBuildModeActive(bool bIsBuildMode, bool bPlayTransition);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ClockText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ClockHourText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ClockColonText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ClockMinuteText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BusinessDayText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> FundsText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ReputationScoreText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ReputationGradeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StoreLevelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StoreLevelNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> StoreExperienceProgressBar;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StoreExperienceText;
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> StatusSizeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Overlay_BuildModeStatus;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> StatusBackgroundImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> StatusIconImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StatusText;

	// C++은 모드 상태만 전달하고, 두 배너의 교차 전환은 WBP가 전담합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Store|Top Bar|Build Mode")
	void OnBuildModeVisualChanged(bool bIsBuildMode, bool bPlayTransition);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Clock")
	FLinearColor ClosingClockColor = FLinearColor(0.85f, 0.12f, 0.08f, 1.0f);

	// 색은 WBP Class Defaults에서 지정하고, 장식용 애니메이션만 필요하면 이 이벤트를 구현합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Store|Top Bar|Status")
	void OnStoreStatusVisualChanged(ECSStoreTopBarStatus Status, bool bAnimate, bool bExpand);
	UFUNCTION(BlueprintImplementableEvent, Category = "Store|Top Bar|Status")
	void OnStoreStatusDetailFinished(ECSStoreTopBarStatus Status);

	UFUNCTION(BlueprintImplementableEvent, Category = "Store|Top Bar|Clock")
	void OnClockTickStateChanged(bool bShouldTick);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status", meta = (ClampMin = "0.0"))
	float StatusDetailSeconds = 2.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status")
	FLinearColor PreparationColor = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status")
	FLinearColor OpenAllowedColor = FLinearColor(0.68f, 1.0f, 0.38f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status")
	FLinearColor OpenPausedColor = FLinearColor(1.0f, 0.75f, 0.3f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status")
	FLinearColor ClosedColor = FLinearColor(0.8f, 0.35f, 0.35f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status", meta = (ClampMin = "1.0"))
	float ShortStatusWidth = 145.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status", meta = (ClampMin = "1.0"))
	float ExpandedStatusWidth = 260.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status", meta = (ClampMin = "0.1"))
	float WidthBlendSpeed = 10.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status", meta = (ClampMin = "0.1"))
	float ColorBlendSpeed = 12.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Top Bar|Status", meta = (ClampMin = "0.1"))
	float TextFadeSpeed = 12.0f;

private:
	UFUNCTION() void HandlePhaseChanged(ECStorePhase NewPhase);
	UFUNCTION() void HandleStoreOpenStateChanged(bool bNewStoreOpen);
	UFUNCTION() void HandleGameTimeChanged(float NewGameMinutes);
	UFUNCTION() void HandleGameClockRunningChanged(bool bNewClockRunning);
	UFUNCTION() void HandleFundsChanged(int32 NewFunds);
	UFUNCTION() void HandleReputationChanged(int32 NewReputationScore);
	UFUNCTION() void HandleReputationGradeChanged(ECSReputationGrade NewGrade);
	UFUNCTION() void HandleBusinessDayStarted(int32 NewBusinessDay);
	UFUNCTION() void HandleStoreExperienceChanged(int32 NewExperience);
	UFUNCTION() void HandleStoreLevelChanged(int32 NewLevel);

	void RefreshStatus(bool bAllowAnimation);
	void UpdateStoreProgressDisplay();
	void ApplyClosingClockColor(float Alpha);
	void SetStatusTextWithBlend(const FText& NewText, bool bAnimate);
	void ApplyStatusColor(const FLinearColor& NewColor);
	UFUNCTION() void CollapseStatusBanner();
	FText GetReputationGradeDisplayName(ECSReputationGrade Grade) const;

	TObjectPtr<ACSStoreGameState> StoreGameState;
	ECSStoreTopBarStatus DisplayedStatus = ECSStoreTopBarStatus::Preparation;
	bool bStatusInitialized = false;
	FText ShortStatusText;
	FText PendingStatusText;
	FLinearColor TargetStatusColor = FLinearColor::White;
	float CurrentStatusWidth = 0.0f;
	float TargetStatusWidth = 0.0f;
	bool bTextFadingOut = false;
	bool bTextFadingIn = false;
	FTimerHandle StatusDetailTimerHandle;
	FLinearColor InitialClockColor = FLinearColor::White;
	FLinearColor InitialClockHourColor = FLinearColor::White;
	FLinearColor InitialClockColonColor = FLinearColor::White;
	FLinearColor InitialClockMinuteColor = FLinearColor::White;
};
