#include "Systems/Audio/CSAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Data/CSAudioConfigDataAsset.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "TimerManager.h"
#include "Engine/World.h"

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
	const FName Id = GetSelectedBGMId();
	USoundBase* Sound = nullptr;
	for (const FCSBGMTrack& Track : GetBGMTracks()) if (Track.Id == Id) { Sound = Track.Sound; break; }
	if (!IsValid(Sound) || !IsValid(GetWorld()))
	{
		return false;
	}

	ClearBGMTransition();
	if (IsValid(ActiveBGMComponent))
	{
		if (bBGMPlaybackRequested && ActiveBGMComponent->Sound == Sound &&
			ActiveBGMComponent->IsPlaying())
		{
			PlayingBGMId = Id;
			OnBGMChanged.Broadcast();
			return true;
		}

		StopBGM();
	}

	bBGMPlaybackRequested = true;
	ActiveBGMComponent = UGameplayStatics::CreateSound2D(
		GetWorld(),
		Sound,
		1.0f,
		1.0f,
		0.0f,
		nullptr,
		false,
		false);

	if (IsValid(ActiveBGMComponent))
	{
		ActiveBGMComponent->SoundClassOverride = AudioConfig->BGMSoundClass;
		ActiveBGMComponent->OnAudioFinished.AddUniqueDynamic(this, &ThisClass::HandleBGMFinished);
		PlayingBGMId = Id;
		ActiveBGMComponent->FadeIn(0.2f);
	}
	OnBGMChanged.Broadcast();
	return IsValid(ActiveBGMComponent);
}

void UCSAudioSubsystem::StopBGM()
{
	bBGMPlaybackRequested = false;
	ClearBGMTransition();
	PlayingBGMId = NAME_None;
	if (!IsValid(ActiveBGMComponent))
	{
		ActiveBGMComponent = nullptr;
		return;
	}

	ActiveBGMComponent->OnAudioFinished.RemoveDynamic(this, &ThisClass::HandleBGMFinished);
	ActiveBGMComponent->Stop();
	ActiveBGMComponent->DestroyComponent();
	ActiveBGMComponent = nullptr;
	OnBGMChanged.Broadcast();
}

void UCSAudioSubsystem::FadeOutBGM(float Duration)
{
	// 수면 중 변경/로드하더라도 다음 PlayBGM 요청 전까지 음악을 다시 켜지 않습니다.
	bBGMPlaybackRequested = false;
	ClearBGMTransition();
	if (IsValid(ActiveBGMComponent))
	{
		ActiveBGMComponent->FadeOut(FMath::Max(0.0f, Duration), 0.0f);
	}
}

TArray<FCSBGMTrack> UCSAudioSubsystem::GetBGMTracks() const
{
	TArray<FCSBGMTrack> Result;
	if (!AudioConfig) return Result;
	if (IsValid(AudioConfig->StoreBGM))
	{
		FCSBGMTrack& Default = Result.AddDefaulted_GetRef();
		Default.Id = TEXT("Store");
		Default.DisplayName = NSLOCTEXT("Jukebox", "StoreMusic", "이세계 편의점");
		Default.Sound = AudioConfig->StoreBGM;
	}
	for (const FCSBGMTrack& Track : AudioConfig->BGMTracks)
	{
		if (Track.Id.IsNone() || !IsValid(Track.Sound)) continue;
		if (FCSBGMTrack* Existing = Result.FindByPredicate([&](const auto& T) { return T.Id == Track.Id; }))
		{
			// Store는 항상 기존 기본 음악입니다. 목록에서는 표시 이름만 바꿀 수 있습니다.
			if (Track.Id == TEXT("Store") && !Track.DisplayName.IsEmpty()) Existing->DisplayName = Track.DisplayName;
			continue;
		}
		Result.Add(Track);
	}
	return Result;
}

FName UCSAudioSubsystem::GetSelectedBGMId() const
{
	for (const auto& Track : GetBGMTracks()) if (Track.Id == SelectedBGMId) return SelectedBGMId;
	return TEXT("Store");
}

FName UCSAudioSubsystem::GetPlayingBGMId() const
{
	return IsValid(ActiveBGMComponent) && ActiveBGMComponent->IsPlaying() ? PlayingBGMId : NAME_None;
}

bool UCSAudioSubsystem::SelectBGM(FName Id)
{
	if (!GetBGMTracks().ContainsByPredicate([&](const auto& T) { return T.Id == Id; })) return false;
	if (GetSelectedBGMId() == Id) return true;
	SelectedBGMId = Id;
	ClearBGMTransition();
	if (bBGMPlaybackRequested)
	{
		if (IsValid(ActiveBGMComponent) && ActiveBGMComponent->IsPlaying() && GetWorld())
		{
			ActiveBGMComponent->FadeOut(0.2f, 0.0f);
			BGMTransitionWorld = GetWorld();
			GetWorld()->GetTimerManager().SetTimer(BGMTransitionTimer, FTimerDelegate::CreateWeakLambda(this, [this] { PlayBGM(); }), 0.2f, false);
		}
		else PlayBGM();
	}
	OnBGMChanged.Broadcast();
	return true;
}

void UCSAudioSubsystem::ClearBGMTransition()
{
	if (BGMTransitionWorld.IsValid()) BGMTransitionWorld->GetTimerManager().ClearTimer(BGMTransitionTimer);
	BGMTransitionWorld.Reset();
}

void UCSAudioSubsystem::HandleBGMFinished()
{
	PlayingBGMId = NAME_None;
	// 페이드 전환은 타이머가 담당하고 자연 종료된 비반복 음원만 다시 재생합니다.
	if (bBGMPlaybackRequested && !BGMTransitionTimer.IsValid()) PlayBGM();
	else OnBGMChanged.Broadcast();
}

void UCSAudioSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	OutSave.SelectedBGMId = GetSelectedBGMId();
}

void UCSAudioSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	SelectedBGMId = InSave.SelectedBGMId;
	SelectedBGMId = GetSelectedBGMId();
	if (bBGMPlaybackRequested) PlayBGM();
	OnBGMChanged.Broadcast();
}

void UCSAudioSubsystem::ResetBGMSelection()
{
	StopBGM();
	SelectedBGMId = NAME_None;
	OnBGMChanged.Broadcast();
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
