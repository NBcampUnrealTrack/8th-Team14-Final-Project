#include "Features/Tutorial/CSTutorialCrateSpawnPoint.h"

const FName ACSTutorialCrateSpawnPoint::DefaultSpawnPointTag(TEXT("TutorialCrateSpawnPoint"));

ACSTutorialCrateSpawnPoint::ACSTutorialCrateSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Tags.Add(DefaultSpawnPointTag);
}
