#include "Features/Delivery/CSDeliveryOrderIndicatorActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

ACSDeliveryOrderIndicatorActor::ACSDeliveryOrderIndicatorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ArrowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowMesh"));
	ArrowMesh->SetupAttachment(SceneRoot);
	ArrowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACSDeliveryOrderIndicatorActor::BeginPlay()
{
	Super::BeginPlay();
	InitialArrowRelativeLocation = ArrowMesh->GetRelativeLocation();
	SetIndicatorVisible(false);
}

void ACSDeliveryOrderIndicatorActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimationElapsedSeconds += DeltaSeconds;

	const float HeightOffset = FMath::Sin(AnimationElapsedSeconds * FloatSpeed) * FloatAmplitude;
	ArrowMesh->SetRelativeLocation(InitialArrowRelativeLocation + FVector(0.0f, 0.0f, HeightOffset));
	ArrowMesh->AddLocalRotation(FRotator(0.0f, RotationSpeedDegreesPerSecond * DeltaSeconds, 0.0f));
}

void ACSDeliveryOrderIndicatorActor::SetIndicatorVisible(const bool bShouldShow)
{
	SetActorHiddenInGame(!bShouldShow);
	SetActorTickEnabled(bShouldShow);
	if (bShouldShow)
	{
		// 다시 표시할 때마다 화살표가 기준 위치에서 자연스럽게 애니메이션을 시작합니다.
		AnimationElapsedSeconds = 0.0f;
		ArrowMesh->SetRelativeLocation(InitialArrowRelativeLocation);
	}
}
