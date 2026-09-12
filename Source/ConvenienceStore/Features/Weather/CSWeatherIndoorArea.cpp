#include "Features/Weather/CSWeatherIndoorArea.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Systems/Weather/CSWeatherSubsystem.h"

ACSWeatherIndoorArea::ACSWeatherIndoorArea()
{
	PrimaryActorTick.bCanEverTick = false;

	IndoorBox = CreateDefaultSubobject<UBoxComponent>(TEXT("IndoorBox"));
	SetRootComponent(IndoorBox);

	// Pawn과의 오버랩만 감지하며 물리적인 이동을 막지 않음
	IndoorBox->SetBoxExtent(FVector(500.0f, 500.0f, 500.0f));
	IndoorBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	IndoorBox->SetCollisionObjectType(ECC_WorldDynamic);
	IndoorBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	IndoorBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	IndoorBox->SetGenerateOverlapEvents(true);
	IndoorBox->SetCanEverAffectNavigation(false);
	IndoorBox->ShapeColor = FColor::Green;
	IndoorBox->OnComponentBeginOverlap.AddDynamic(
		this,
		&ACSWeatherIndoorArea::HandleIndoorBoxBeginOverlap);
	IndoorBox->OnComponentEndOverlap.AddDynamic(
		this,
		&ACSWeatherIndoorArea::HandleIndoorBoxEndOverlap);
}

void ACSWeatherIndoorArea::BeginPlay()
{
	Super::BeginPlay();

	// 플레이어가 이미 실내에서 시작한 경우도 등록 시 현재 상태를 재판정
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->RegisterIndoorArea(this);
		}
	}
}

void ACSWeatherIndoorArea::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->UnregisterIndoorArea(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ACSWeatherIndoorArea::HandleIndoorBoxBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->NotifyShelterOverlapChanged(OtherActor);
		}
	}
}

void ACSWeatherIndoorArea::HandleIndoorBoxEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->NotifyShelterOverlapChanged(OtherActor);
		}
	}
}

// 지정한 액터가 실내 박스와 겹쳐 있는지 반환
// 박스나 액터가 유효하지 않으면 false를 반환
bool ACSWeatherIndoorArea::IsActorInside(const AActor* Actor) const
{
	return IsValid(IndoorBox) && IsValid(Actor) && IndoorBox->IsOverlappingActor(Actor);
}
