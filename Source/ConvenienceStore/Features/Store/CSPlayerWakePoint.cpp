#include "Features/Store/CSPlayerWakePoint.h"
#include "Components/ArrowComponent.h"

ACSPlayerWakePoint::ACSPlayerWakePoint()
{
	PrimaryActorTick.bCanEverTick = false;
	WakeDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("WakeDirection"));
	SetRootComponent(WakeDirection);
	WakeDirection->ArrowColor = FColor(80, 200, 255);
	WakeDirection->ArrowSize = 2.0f;
	WakeDirection->SetHiddenInGame(true);
}
