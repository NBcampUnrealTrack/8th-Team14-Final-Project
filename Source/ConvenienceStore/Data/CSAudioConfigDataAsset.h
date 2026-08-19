#pragma once

#include "CoreMinimal.h"
#include "Data/CSAudioTypes.h"
#include "Engine/DataAsset.h"
#include "CSAudioConfigDataAsset.generated.h"

class USoundBase;
class USoundClass;
class USoundMix;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|SFX")
	TMap<ECSAudioEvent, TObjectPtr<USoundBase>> SFXSounds;
};
