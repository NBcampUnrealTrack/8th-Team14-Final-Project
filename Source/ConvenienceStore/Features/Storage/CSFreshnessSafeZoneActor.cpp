#include "Features/Storage/CSFreshnessSafeZoneActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Items/CSProductPickupActor.h"

ACSFreshnessSafeZoneActor::ACSFreshnessSafeZoneActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SafeZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SafeZoneBox"));
	SafeZoneBox->SetupAttachment(SceneRoot);
	SafeZoneBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SafeZoneBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	SafeZoneBox->SetGenerateOverlapEvents(true);
	SafeZoneBox->OnComponentBeginOverlap.AddDynamic(
		this,
		&ACSFreshnessSafeZoneActor::HandleSafeZoneBeginOverlap);
	SafeZoneBox->OnComponentEndOverlap.AddDynamic(
		this,
		&ACSFreshnessSafeZoneActor::HandleSafeZoneEndOverlap);
}

void ACSFreshnessSafeZoneActor::BeginPlay()
{
	Super::BeginPlay();

	if (!IsSupportedSafeStorageType())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FreshnessSafeZone] %s의 SafeStorageType은 냉장 또는 냉동 타입이 아니므로 효과를 적용하지 않습니다."),
			*GetName());
		return;
	}

	TArray<AActor*> InitiallyOverlappingActors;
	SafeZoneBox->GetOverlappingActors(InitiallyOverlappingActors);
	for (AActor* OverlappingActor : InitiallyOverlappingActors)
	{
		RegisterOverlappingActor(OverlappingActor);
	}
}

void ACSFreshnessSafeZoneActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const TArray<TWeakObjectPtr<AActor>> ActorsToUnregister = RegisteredActors.Array();
	for (const TWeakObjectPtr<AActor>& RegisteredActor : ActorsToUnregister)
	{
		if (AActor* Actor = RegisteredActor.Get())
		{
			UnregisterOverlappingActor(Actor);
		}
	}
	RegisteredActors.Reset();

	Super::EndPlay(EndPlayReason);
}

void ACSFreshnessSafeZoneActor::HandleSafeZoneBeginOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32,
	bool,
	const FHitResult&)
{
	RegisterOverlappingActor(OtherActor);
}

void ACSFreshnessSafeZoneActor::HandleSafeZoneEndOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32)
{
	UnregisterOverlappingActor(OtherActor);
}

bool ACSFreshnessSafeZoneActor::IsSupportedSafeStorageType() const
{
	return SafeStorageType == ECSProductShelfType::ChilledShelf ||
		SafeStorageType == ECSProductShelfType::FreezerShelf;
}

void ACSFreshnessSafeZoneActor::RegisterOverlappingActor(AActor* OtherActor)
{
	if (!IsSupportedSafeStorageType() || !IsValid(OtherActor) ||
		RegisteredActors.Contains(OtherActor))
	{
		return;
	}

	if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(OtherActor))
	{
		RegisteredActors.Add(OtherActor);
		ProductActor->EnterFreshnessSafeZone(SafeStorageType);
	}
	else if (ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(OtherActor))
	{
		RegisteredActors.Add(OtherActor);
		DeliveryCrate->EnterFreshnessSafeZone(SafeStorageType);
	}
}

void ACSFreshnessSafeZoneActor::UnregisterOverlappingActor(AActor* OtherActor)
{
	if (!IsValid(OtherActor) || RegisteredActors.Remove(OtherActor) == 0)
	{
		return;
	}

	if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(OtherActor))
	{
		ProductActor->ExitFreshnessSafeZone(SafeStorageType);
	}
	else if (ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(OtherActor))
	{
		DeliveryCrate->ExitFreshnessSafeZone(SafeStorageType);
	}
}
