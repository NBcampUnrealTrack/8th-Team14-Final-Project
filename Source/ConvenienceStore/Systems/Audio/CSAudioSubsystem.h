#pragma once

#include "CoreMinimal.h"
#include "Data/CSAudioTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSAudioSubsystem.generated.h"

class UAudioComponent;
class UCSAudioConfigDataAsset;
class USoundBase;

// 레벨 전환과 관계없이 볼륨 상태를 유지하며 BGM/SFX를 중앙 관리합니다.
UCLASS()
class CONVENIENCESTORE_API UCSAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// WBP 등 서브시스템 참조가 필요한 곳에서 GameInstance를 거치지 않고 바로 가져다 쓰기 위한 접근자.
	UFUNCTION(BlueprintPure, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	static UCSAudioSubsystem* Get(const UObject* WorldContextObject);

	bool InitializeAudio(UCSAudioConfigDataAsset* InAudioConfig);

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

	UFUNCTION(BlueprintCallable, Category = "Audio|BGM")
	void StopBGM();

	// SFX SoundClass에는 영향을 주지 않고 현재 BGM 컴포넌트만 정지까지 페이드합니다.
	void FadeOutBGM(float Duration);
	float GetSFXDuration(ECSAudioEvent AudioEvent) const;

	UFUNCTION(BlueprintCallable, Category = "Audio|SFX")
	bool PlaySFX(ECSAudioEvent AudioEvent);

	// 호출 측에서 완료 Delegate를 먼저 연결할 수 있도록 재생 전 2D 컴포넌트만 생성합니다.
	UAudioComponent* CreateTrackedSFX2D(ECSAudioEvent AudioEvent);

	UFUNCTION(BlueprintCallable, Category = "Audio|SFX")
	bool PlaySFXAtLocation(ECSAudioEvent AudioEvent, FVector Location);

private:
	void ApplyVolumeSettings();
	void ReleaseSoundMix();
	USoundBase* FindSFXSound(ECSAudioEvent AudioEvent) const;

	UPROPERTY(Transient)
	TObjectPtr<UCSAudioConfigDataAsset> AudioConfig;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveBGMComponent;

	float BGMVolume = 1.0f;
	float SFXVolume = 1.0f;
	bool bBGMMuted = false;
	bool bSFXMuted = false;
	bool bSoundMixPushed = false;
};
