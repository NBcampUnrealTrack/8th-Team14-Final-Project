#include "Features/Weather/CSWeatherBlockerVolume.h"

#include "Features/Weather/CSWeatherBlockerComponent.h"

ACSWeatherBlockerVolume::ACSWeatherBlockerVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// 박스 등록과 해제는 컴포넌트의 BeginPlay·EndPlay에서 처리
	BlockerBox = CreateDefaultSubobject<UCSWeatherBlockerComponent>(TEXT("BlockerBox"));
	SetRootComponent(BlockerBox);
}
