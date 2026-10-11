#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSLevelStoryData.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class ECSStorySpeaker : uint8
{
	Player,
	Other,
	Narration,
	// 기존 데이터 테이블의 enum 인덱스를 유지하기 위해 새 항목은 마지막에 추가합니다.
	PlayerThought
};

UENUM(BlueprintType)
enum class ECSStoryBackground : uint8 { PlayerView, Illustration, TaggedCamera };

USTRUCT(BlueprintType)
struct FCSStoryLine
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ECSStorySpeaker Speaker = ECSStorySpeaker::Player;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText SpeakerName;
	// Other 대사에서 지정하면 이후 플레이어 대사 중에도 유지되고, 스토리가 끝날 때 사라집니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> SpeakerPortrait;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (MultiLine = "true")) FText Text;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1")) float CharactersPerSecond = 30.0f;
};

USTRUCT(BlueprintType)
struct FCSStoryScene
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName SceneId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ECSStoryBackground Background = ECSStoryBackground::PlayerView;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Illustration;
	// 지정 카메라 배경에서는 레벨의 카메라 액터에 같은 Actor Tag를 입력해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CameraTag;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Caption;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCSStoryLine> Lines;
};

USTRUCT(BlueprintType)
struct FCSLevelStoryEvent : public FTableRowBase
{
	GENERATED_BODY()
	// 1은 새 게임 오프닝이며, 2~10은 해당 레벨 달성 시 재생됩니다. 레벨마다 한 행만 등록해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "10")) int32 StoreLevel = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText LevelUpMessage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (TitleProperty = "SceneId")) TArray<FCSStoryScene> Scenes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (MultiLine = "true")) FText NextGoal;
};

