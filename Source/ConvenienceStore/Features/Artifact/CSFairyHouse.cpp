#include "Features/Artifact/CSFairyHouse.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

ACSFairyHouse::ACSFairyHouse()
{
	PrimaryActorTick.bCanEverTick = false;
	HouseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HouseMesh"));
	SetRootComponent(HouseMesh);
	HouseMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Demo/KJW/FairyHouse/SM_ConvenienceStore_FairyHouse.SM_ConvenienceStore_FairyHouse")));
	// 지정된 카운터 위의 장식이므로 손님 동선과 상호작용 시야를 막지 않도록 합니다.
	HouseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

ACSFairyHousePlacementPoint::ACSFairyHousePlacementPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	PlacementPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("PlacementPoint"));
	PlacementPoint->SetupAttachment(RootComponent);
	PlacementPoint->SetHiddenInGame(true);
}
