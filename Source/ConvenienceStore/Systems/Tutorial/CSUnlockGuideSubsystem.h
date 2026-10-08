#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "Data/CSUnlockGuideData.h"
#include "Data/CSLevelStoryData.h"
#include "Components/SlateWrapperTypes.h"
#include "CSUnlockGuideSubsystem.generated.h"

class ACSStoreGameState;
class ACSStorePlayerController;
class ACSUnlockGuideCamera;
class ACameraActor;
class UCSUnlockGuideWidget;
class UWidget;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class UCSLevelPresentationWidget;
class UCSWakeUpWidget;

// 해금 알림과 카메라 안내를 관리하며, 게임 정지 중에는 실제 경과 시간으로 연출합니다.
UCLASS()
class CONVENIENCESTORE_API UCSUnlockGuideSubsystem : public UTickableWorldSubsystem, public ICSSaveParticipant
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;
	virtual int32 GetSaveRestorePriority() const override { return 15; }
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;
	UFUNCTION(BlueprintCallable, Category = "Unlock Guide") void ViewGuide();
	UFUNCTION(BlueprintCallable, Category = "Unlock Guide") void DeferGuide();
	UFUNCTION(BlueprintCallable, Category = "Unlock Guide") void DismissGuide();
	UFUNCTION(BlueprintCallable, Category = "Unlock Guide") void CancelRequest();
	UFUNCTION(BlueprintCallable, Category = "Unlock Guide") void SkipCamera();
	UFUNCTION(BlueprintCallable, Category = "Unlock Guide") void CloseSummary();
	// 확인 또는 생략한 안내도 이 함수와 기본 F10 키로 다시 읽으실 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "Unlock Guide") void ShowGuideHistory();
	UFUNCTION(BlueprintCallable, Category = "Level Story") void ContinuePresentation();
	UFUNCTION(BlueprintCallable, Category = "Level Story") void SkipStoryScene();
	bool StartOpeningStory();
	bool IsPresentationActive() const { return bOpening || bLevelEvent; }
private:
	struct FPlaybackShot
	{
		FText Title;
		FText Description;
		TWeakObjectPtr<ACSUnlockGuideCamera> Camera;
		float Duration = 2.5f;
	};
	enum class EStage : uint8 { Idle, Waiting, EyeOpening, LevelUp, Story, Camera, CameraHold, Returning, Summary, Goal };
	EStage Stage = EStage::Idle;
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> StoreState;
	UPROPERTY(Transient) TObjectPtr<ACSStorePlayerController> Controller;
	UPROPERTY(Transient) TObjectPtr<UCSUnlockGuideWidget> Widget;
	UPROPERTY(Transient) TObjectPtr<UCSLevelPresentationWidget> PresentationWidget;
	UPROPERTY(Transient) TObjectPtr<UCSWakeUpWidget> EyeWidget;
	UPROPERTY(Transient) TArray<FCSLevelStoryEvent> StoryEvents;
	TArray<int32> PendingLevels;
	TArray<int32> CompletedLevels;
	TArray<FCSStoryScene> ActiveScenes;
	FText ActiveGoal;
	int32 ActiveLevel = INDEX_NONE;
	int32 SceneIndex = INDEX_NONE;
	int32 LineIndex = 0;
	int32 VisibleCharacters = 0;
	float TextElapsed = 0.0f;
	bool bOpening = false;
	bool bLevelEvent = false;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> PlaybackCamera;
	UPROPERTY(Transient) TObjectPtr<ULevelSequencePlayer> SequencePlayer;
	UPROPERTY(Transient) TObjectPtr<ALevelSequenceActor> SequenceActor;
	TArray<FCSUnlockGuideEntry> Entries;
	TMap<FName, TWeakObjectPtr<ACSUnlockGuideCamera>> Cameras;
	TArray<FName> Pending;
	TArray<FName> Seen;
	TArray<FName> NotificationIds;
	TArray<FName> RequestIds;
	TArray<FName> Batch;
	// 팝업과 저장은 해금 ID 단위로 유지하고, 카메라 재생 목록만 장면 단위로 펼칩니다.
	TArray<FPlaybackShot> PlaybackShots;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TMap<TWeakObjectPtr<UWidget>, ESlateVisibility> HiddenWidgets;
	FDelegateHandle WidgetAddedHandle;
	int32 ObservedLevel = INDEX_NONE;
	int32 ShotIndex = INDEX_NONE;
	float Elapsed = 0.0f;
	float StableTime = 0.0f;
	float AutoCollapseTime = 0.0f;
	double LastRealTime = 0.0;
	bool bConfigured = false;
	bool bDeferred = false;
	bool bHistory = false;
	bool bOwnsPause = false;
	bool bPreviousFullTick = false;
	bool bShuttingDown = false;
	bool bFadeReturn = false;
	FTransform ShotStart;
	FTransform ShotEnd;
	FTransform ReturnStart;
	FTransform ReturnEnd;
	float ShotFOV = 90.0f;
	float ReturnFOV = 90.0f;
	float SequenceStartSeconds = 0.0f;
	float SequenceDuration = 0.0f;
	void Configure();
	UFUNCTION() void HandleLevelChanged(int32 NewLevel);
	bool CanStart() const;
	void BeginGuide();
	bool EnsurePresentationWidget();
	void BeginNextLevel();
	void BeginStory();
	void NextStoryScene();
	void EnterStoryLine();
	void RefreshPresentation();
	void ShowGoal();
	UFUNCTION() void FinishEyeOpening();
	void BuildPlaybackShots();
	void NextShot();
	void HoldCamera();
	void BeginReturn();
	void UpdateCamera(float RealDelta);
	void RefreshWidget();
	void HideOtherWidgets();
	void RestoreOtherWidgets();
	void HandleWidgetAdded(UWidget* AddedWidget, ULocalPlayer* Player);
	void Finish(bool bMarkSeen);
	void ClearSequence();
	const FCSUnlockGuideEntry* FindEntry(FName Id) const;
	const FCSLevelStoryEvent* FindStoryEvent(int32 Level) const;
};
