#pragma once

#include "CoreMinimal.h"
#include "Data/CSAudioTypes.h"
#include "Data/CSAudioConfigDataAsset.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSAudioSubsystem.generated.h"

class UAudioComponent;
class UCSAudioConfigDataAsset;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSBGMChanged);

// 레벨 전환과 관계없이 볼륨 상태를 유지하며 BGM/SFX를 중앙 관리합니다.
UCLASS()
class CONVENIENCESTORE_API UCSAudioSubsystem : public UGameInstanceSubsystem, public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// WBP 등 서브시스템 참조가 필요한 곳에서 GameInstance를 거치지 않고 바로 가져다 쓰기 위한 접근자.
	UFUNCTION(BlueprintPure, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	static UCSAudioSubsystem* Get(const UObject* WorldContextObject);

	bool InitializeAudio(UCSAudioConfigDataAsset* InAudioConfig);

	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;
	void ResetBGMSelection();

	UFUNCTION(BlueprintPure, Category = "Audio|BGM") TArray<FCSBGMTrack> GetBGMTracks() const;
	UFUNCTION(BlueprintPure, Category = "Audio|BGM") FName GetSelectedBGMId() const;
	UFUNCTION(BlueprintPure, Category = "Audio|BGM") FName GetPlayingBGMId() const;
	UFUNCTION(BlueprintCallable, Category = "Audio|BGM") bool SelectBGM(FName Id);
	UPROPERTY(BlueprintAssignable, Category = "Audio|BGM") FCSBGMChanged OnBGMChanged;

	UFUNCTION(BlueprintCallable, Category = "Audio|Volume")
	void SetBGMVolume(float NewVolume);

	UFUNCTION(BlueprintCallable, Category = "Audio|Volume")
	void SetSFXVolume(float NewVolume);

	UFUNCTION(BlueprintCallable, Category = "Audio|Volume")
	void SetBGMMuted(bool bMuted);

	UFUNCTION(BlueprintCallable, Category = "Audio|Volume")
	void SetSFXMuted(bool bMuted);

	UFUNCTION(BlueprintPure, Category = "Audio|Volume")
	float GetBGMVolume() const;

	UFUNCTION(BlueprintPure, Category = "Audio|Volume")
	float GetSFXVolume() const;

	UFUNCTION(BlueprintPure, Category = "Audio|Volume")
	bool IsBGMMuted() const;

	UFUNCTION(BlueprintPure, Category = "Audio|Volume")
	bool IsSFXMuted() const;

	UFUNCTION(BlueprintCallable, Category = "Audio|BGM")
	bool PlayBGM();

	// 메뉴 음악처럼 저장된 주크박스 선택을 바꾸지 않는 일시적인 BGM 요청입니다.
	bool PlayBGMTrack(FName Id);

	UFUNCTION(BlueprintCallable, Category = "Audio|BGM")
	void StopBGM();

	// SFX SoundClass에는 영향을 주지 않고 현재 BGM 컴포넌트만 정지까지 페이드합니다.
	void FadeOutBGM(float Duration);
	float GetSFXDuration(ECSAudioEvent AudioEvent) const;

	UFUNCTION(BlueprintCallable, Category = "Audio|SFX")
	bool PlaySFX(ECSAudioEvent AudioEvent);

	// 호출 측에서 완료 Delegate를 먼저 연결할 수 있도록 재생 전 2D 컴포넌트만 생성합니다.
	UAudioComponent* CreateTrackedSFX2D(ECSAudioEvent AudioEvent, bool bPersistAcrossLevelTransition = false);

	UFUNCTION(BlueprintCallable, Category = "Audio|SFX")
	bool PlaySFXAtLocation(ECSAudioEvent AudioEvent, FVector Location);

private:
	void ApplyVolumeSettings();
	void ReleaseSoundMix();
	USoundBase* FindSFXSound(ECSAudioEvent AudioEvent) const;
	void ClearBGMTransition();
	void HandlePersistentSFXFinished(UAudioComponent* Component);
	UFUNCTION() void HandleBGMFinished();
	FName SelectedBGMId = NAME_None;
	FName PlayingBGMId = NAME_None;
	bool bBGMPlaybackRequested = false;
	FTimerHandle BGMTransitionTimer;
	TWeakObjectPtr<UWorld> BGMTransitionWorld;

	UPROPERTY(Transient)
	TObjectPtr<UCSAudioConfigDataAsset> AudioConfig;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveBGMComponent;

	// 레벨 전환 GC에서도 짧은 시작 효과음이 완료될 때까지 참조를 유지합니다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> PersistentSFXComponents;

	float BGMVolume = 1.0f;
	float SFXVolume = 1.0f;
	bool bBGMMuted = false;
	bool bSFXMuted = false;
	bool bSoundMixPushed = false;
};
