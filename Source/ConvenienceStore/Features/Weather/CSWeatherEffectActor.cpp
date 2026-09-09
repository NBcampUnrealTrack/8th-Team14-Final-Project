#include "Features/Weather/CSWeatherEffectActor.h"

#include "Components/SceneComponent.h"

ACSWeatherEffectActor::ACSWeatherEffectActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ACSWeatherEffectActor::StartEffect_Implementation()
{
	// 기본 구현은 비어 있습니다. 나이아가라 Activate는 상속 BP가 합니다.
}

void ACSWeatherEffectActor::StopEffect_Implementation()
{
	if (bStopRequested)
	{
		// 이미 종료 절차가 시작되었습니다. Destroy를 다시 부르지 않습니다.
		return;
	}
	bStopRequested = true;

	// 정산 UI를 닫는 순간이 곧 다음 날이므로 잦아들 시간을 두지 않습니다.
	// 액터와 함께 나이아가라 컴포넌트가 지워지면서 남은 파티클도 같은 프레임에 사라집니다.
	Destroy();
}
