#include "Features/Tutorial/CSTutorialShelfSpawnPoint.h"

const FName ACSTutorialShelfSpawnPoint::DefaultSpawnPointTag(TEXT("TutorialShelfSpawnPoint"));

ACSTutorialShelfSpawnPoint::ACSTutorialShelfSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 기본 태그를 자동으로 붙여둠
	Tags.Add(DefaultSpawnPointTag);
}
