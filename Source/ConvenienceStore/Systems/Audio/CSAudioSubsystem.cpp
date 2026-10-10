#include "Systems/Audio/CSAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Data/CSAudioConfigDataAsset.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

void UCSAudioSubsystem::Deinitialize()
{
	StopBGM();
	ReleaseSoundMix();
	AudioConfig = nullptr;

	Super::Deinitialize();
}

UCSAudioSubsystem* UCSAudioSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
	return IsValid(GameInstance) ? GameInstance->GetSubsystem<UCSAudioSubsystem>() : nullptr;
}

bool UCSAudioSubsystem::InitializeAudio(UCSAudioConfigDataAsset* InAudioConfig)
{
	if (!IsValid(InAudioConfig) ||
		!IsValid(InAudioConfig->MasterSoundMix) ||
		!IsValid(InAudioConfig->BGMSoundClass) ||
		!IsValid(InAudioConfig->SFXSoundClass))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AudioSubsystem] AudioConfig 또는 SoundMix/SoundClass 설정이 유효하지 않습니다."));
		return false;
	}

	if (AudioConfig != InAudioConfig)
	{
		StopBGM();
		ReleaseSoundMix();
		AudioConfig = InAudioConfig;
	}

	if (!bSoundMixPushed)
	{
		UGameplayStatics::PushSoundMixModifier(
			GetGameInstance(), AudioConfig->MasterSoundMix);
		bSoundMixPushed = true;
	}

	ApplyVolumeSettings();
	return true;
}

void UCSAudioSubsystem::SetBGMVolume(float NewVolume)
{
	const float ClampedVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(BGMVolume, ClampedVolume))
	{
		return;
	}

	BGMVolume = ClampedVolume;
	ApplyVolumeSettings();
}

void UCSAudioSubsystem::SetSFXVolume(float NewVolume)
{
	const float ClampedVolume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(SFXVolume, ClampedVolume))
	{
		return;
	}

	SFXVolume = ClampedVolume;
	ApplyVolumeSettings();
}

void UCSAudioSubsystem::SetBGMMuted(bool bMuted)
{
	if (bBGMMuted == bMuted)
	{
		return;
	}

	bBGMMuted = bMuted;
	ApplyVolumeSettings();
}

void UCSAudioSubsystem::SetSFXMuted(bool bMuted)
{
	if (bSFXMuted == bMuted)
	{
		return;
	}

	bSFXMuted = bMuted;
	ApplyVolumeSettings();
}

float UCSAudioSubsystem::GetBGMVolume() const
{
	return BGMVolume;
}

float UCSAudioSubsystem::GetSFXVolume() const
{
	return SFXVolume;
}

bool UCSAudioSubsystem::IsBGMMuted() const
{
	return bBGMMuted;
}

bool UCSAudioSubsystem::IsSFXMuted() const
{
	return bSFXMuted;
}

bool UCSAudioSubsystem::PlayBGM()
{
	if (!IsValid(AudioConfig) || !IsValid(AudioConfig->StoreBGM) || !IsValid(GetWorld()))
	{
		return false;
	}

	if (IsValid(ActiveBGMComponent))
	{
		if (ActiveBGMComponent->Sound == AudioConfig->StoreBGM &&
			ActiveBGMComponent->IsPlaying())
		{
			return true;
		}

		StopBGM();
	}

	ActiveBGMComponent = UGameplayStatics::SpawnSound2D(
		GetWorld(),
		AudioConfig->StoreBGM,
		1.0f,
		1.0f,
		0.0f,
		nullptr,
		false,
		false);

	return IsValid(ActiveBGMComponent);
}

void UCSAudioSubsystem::StopBGM()
{
	if (!IsValid(ActiveBGMComponent))
	{
		ActiveBGMComponent = nullptr;
		return;
	}

	ActiveBGMComponent->Stop();
	ActiveBGMComponent->DestroyComponent();
	ActiveBGMComponent = nullptr;
}

void UCSAudioSubsystem::FadeOutBGM(float Duration)
{
	if (IsValid(ActiveBGMComponent))
	{
		ActiveBGMComponent->FadeOut(FMath::Max(0.0f, Duration), 0.0f);
	}
}

float UCSAudioSubsystem::GetSFXDuration(ECSAudioEvent AudioEvent) const
{
	const USoundBase* Sound = FindSFXSound(AudioEvent);
	return IsValid(Sound) && !Sound->IsLooping() && FMath::IsFinite(Sound->GetDuration())
		? FMath::Max(0.0f, Sound->GetDuration()) : 0.0f;
}

bool UCSAudioSubsystem::PlaySFX(ECSAudioEvent AudioEvent)
{
	USoundBase* Sound = FindSFXSound(AudioEvent);
	if (!IsValid(Sound) || !IsValid(GetWorld()))
	{
		return false;
	}

	UGameplayStatics::PlaySound2D(GetWorld(), Sound);
	return true;
}

UAudioComponent* UCSAudioSubsystem::CreateTrackedSFX2D(ECSAudioEvent AudioEvent)
{
	USoundBase* Sound = FindSFXSound(AudioEvent);
	if (!IsValid(Sound) || !IsValid(GetWorld()))
	{
		return nullptr;
	}

	if (Sound->IsLooping())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AudioSubsystem] 반복 재생 Sound는 완료 시점을 추적할 수 없어 SFX 생성을 생략합니다: %s"),
			*UEnum::GetValueAsString(AudioEvent));
		return nullptr;
	}

	return UGameplayStatics::CreateSound2D(
		GetWorld(),
		Sound,
		1.0f,
		1.0f,
		0.0f,
		nullptr,
		false,
		true);
}

bool UCSAudioSubsystem::PlaySFXAtLocation(
	ECSAudioEvent AudioEvent,
	FVector Location)
{
	USoundBase* Sound = FindSFXSound(AudioEvent);
	if (!IsValid(Sound) || !IsValid(GetWorld()))
	{
		return false;
	}

	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Location);
	return true;
}

void UCSAudioSubsystem::ApplyVolumeSettings()
{
	if (!IsValid(AudioConfig) ||
		!IsValid(AudioConfig->MasterSoundMix) ||
		!IsValid(AudioConfig->BGMSoundClass) ||
		!IsValid(AudioConfig->SFXSoundClass) ||
		!IsValid(GetGameInstance()))
	{
		return;
	}

	const float EffectiveBGMVolume = bBGMMuted ? 0.0f : BGMVolume;
	const float EffectiveSFXVolume = bSFXMuted ? 0.0f : SFXVolume;
	UGameplayStatics::SetSoundMixClassOverride(
		GetGameInstance(),
		AudioConfig->MasterSoundMix,
		AudioConfig->BGMSoundClass,
		EffectiveBGMVolume,
		1.0f,
		0.0f,
		true);
	UGameplayStatics::SetSoundMixClassOverride(
		GetGameInstance(),
		AudioConfig->MasterSoundMix,
		AudioConfig->SFXSoundClass,
		EffectiveSFXVolume,
		1.0f,
		0.0f,
		true);
}

void UCSAudioSubsystem::ReleaseSoundMix()
{
	if (!bSoundMixPushed || !IsValid(AudioConfig) || !IsValid(GetGameInstance()))
	{
		bSoundMixPushed = false;
		return;
	}

	if (IsValid(AudioConfig->MasterSoundMix))
	{
		if (IsValid(AudioConfig->BGMSoundClass))
		{
			UGameplayStatics::ClearSoundMixClassOverride(
				GetGameInstance(),
				AudioConfig->MasterSoundMix,
				AudioConfig->BGMSoundClass,
				0.0f);
		}

		if (IsValid(AudioConfig->SFXSoundClass))
		{
			UGameplayStatics::ClearSoundMixClassOverride(
				GetGameInstance(),
				AudioConfig->MasterSoundMix,
				AudioConfig->SFXSoundClass,
				0.0f);
		}

		UGameplayStatics::PopSoundMixModifier(
			GetGameInstance(), AudioConfig->MasterSoundMix);
	}

	bSoundMixPushed = false;
}

USoundBase* UCSAudioSubsystem::FindSFXSound(ECSAudioEvent AudioEvent) const
{
	if (!IsValid(AudioConfig))
	{
		return nullptr;
	}

	const TObjectPtr<USoundBase>* Sound = AudioConfig->SFXSounds.Find(AudioEvent);
	return Sound != nullptr ? Sound->Get() : nullptr;
}
