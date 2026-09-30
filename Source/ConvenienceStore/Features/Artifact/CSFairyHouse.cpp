#include "Features/Artifact/CSFairyHouse.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "Features/Artifact/CSCleaningFairy.h"
#include "Engine/World.h"

ACSFairyHouse::ACSFairyHouse()
{
	PrimaryActorTick.bCanEverTick = false;
	HouseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HouseMesh"));
	SetRootComponent(HouseMesh);
	HouseMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Demo/KJW/FairyHouse/SM_ConvenienceStore_FairyHouse.SM_ConvenienceStore_FairyHouse")));
	// 지정된 카운터 위의 장식이므로 손님 동선과 상호작용 시야를 막지 않도록 합니다.
	HouseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DoorPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("DoorPoint"));
	DoorPoint->SetupAttachment(HouseMesh);
	DoorPoint->SetRelativeLocation(FVector(0, -80, 40));
	FlightPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("FlightPoint"));
	FlightPoint->SetupAttachment(HouseMesh);
	FlightPoint->SetRelativeLocation(FVector(0, -200, 150));
	CleaningArea = CreateDefaultSubobject<UBoxComponent>(TEXT("CleaningArea"));
	CleaningArea->SetupAttachment(HouseMesh);
	CleaningArea->SetBoxExtent(FVector(1500, 1500, 500));
	CleaningArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FairyClass = ACSCleaningFairy::StaticClass();
}

void ACSFairyHouse::BeginPlay()
{
	Super::BeginPlay();
	if (!FairyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] %s의 FairyClass가 비어 있어 요정을 생성할 수 없습니다."), *GetName());
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActiveFairy = GetWorld()->SpawnActor<ACSCleaningFairy>(FairyClass, DoorPoint->GetComponentLocation(), GetActorRotation(), Params);
	if (ActiveFairy) ActiveFairy->Initialize(this);
	else UE_LOG(LogTemp, Warning, TEXT("[CleaningFairy] %s에서 요정 생성에 실패했습니다."), *GetName());
}

void ACSFairyHouse::EndPlay(const EEndPlayReason::Type Reason)
{
	if (IsValid(ActiveFairy)) ActiveFairy->Destroy();
	Super::EndPlay(Reason);
}

ACSFairyHousePlacementPoint::ACSFairyHousePlacementPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	PlacementPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("PlacementPoint"));
	PlacementPoint->SetupAttachment(RootComponent);
	PlacementPoint->SetHiddenInGame(true);
}
