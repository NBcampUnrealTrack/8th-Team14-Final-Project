#include "Features/Weather/CSWeatherCoverageVolume.h"

#include "Features/Weather/CSWeatherCoverageComponent.h"

ACSWeatherCoverageVolume::ACSWeatherCoverageVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// 박스 등록과 해제는 컴포넌트의 BeginPlay·EndPlay에서 처리
	CoverageBox = CreateDefaultSubobject<UCSWeatherCoverageComponent>(TEXT("CoverageBox"));
	SetRootComponent(CoverageBox);
}
