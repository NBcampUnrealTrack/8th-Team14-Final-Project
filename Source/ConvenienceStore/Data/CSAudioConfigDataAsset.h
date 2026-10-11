#pragma once

#include "CoreMinimal.h"
#include "Data/CSAudioTypes.h"
#include "Engine/DataAsset.h"
#include "CSAudioConfigDataAsset.generated.h"

class USoundBase;
class USoundClass;
class USoundMix;

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSBGMTrack
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BGM") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BGM") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BGM") TObjectPtr<USoundBase> Sound;
};

// BGM과 이벤트별 SFX 에셋 및 SoundMix 라우팅을 한 곳에서 관리합니다.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSAudioConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Mix")
	TObjectPtr<USoundMix> MasterSoundMix;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Mix")
	TObjectPtr<USoundClass> BGMSoundClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Mix")
	TObjectPtr<USoundClass> SFXSoundClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|BGM")
	TObjectPtr<USoundBase> StoreBGM;

	// 고유 ID를 유지하면 표시 이름/음원을 바꿔도 기존 세이브의 선택이 유지됩니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|BGM")
	TArray<FCSBGMTrack> BGMTracks;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|SFX")
	TMap<ECSAudioEvent, TObjectPtr<USoundBase>> SFXSounds;
};
