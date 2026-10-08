#include "Features/Store/CSUnlockGuideCamera.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"

ACSUnlockGuideCamera::ACSUnlockGuideCamera()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	GuideCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("GuideCamera"));
	GuideCamera->SetupAttachment(SceneRoot);
	EndPoint = CreateDefaultSubobject<USceneComponent>(TEXT("EndPoint"));
	EndPoint->SetupAttachment(SceneRoot);
	EndPoint->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
}
