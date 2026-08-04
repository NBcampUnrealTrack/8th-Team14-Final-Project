#include "Systems/TimeOfDay/CSNightLightActor.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

ACSNightLightActor::ACSNightLightActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	NightLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("NightLight"));
	NightLight->SetupAttachment(SceneRoot);
	NightLight->SetMobility(EComponentMobility::Movable);
	NightLight->SetVisibility(false);
	NightLight->SetIntensity(0.0f);
}

void ACSNightLightActor::BeginPlay()
{
	Super::BeginPlay();

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnGameTimeChanged.AddDynamic(
			this, &ACSNightLightActor::HandleGameTimeChanged);
		HandleGameTimeChanged(StoreGameState->CurrentGameMinutes);
	}
}

void ACSNightLightActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnGameTimeChanged.RemoveDynamic(
			this, &ACSNightLightActor::HandleGameTimeChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void ACSNightLightActor::HandleGameTimeChanged(float NewGameMinutes)
{
	const float Brightness = GetNightBrightness(NewGameMinutes);
	NightLight->SetLightColor(NightLightColor);
	NightLight->SetIntensity(NightLightIntensity * Brightness);
	NightLight->SetVisibility(Brightness > KINDA_SMALL_NUMBER);
}

float ACSNightLightActor::GetNightBrightness(float GameMinutes) const
{
	const float CurrentTime = FMath::Fmod(GameMinutes, 1440.0f);
	const float NightDuration = FMath::Fmod(
		NightEndMinutes - NightStartMinutes + 1440.0f, 1440.0f);
	const float ElapsedNightMinutes = FMath::Fmod(
		CurrentTime - NightStartMinutes + 1440.0f, 1440.0f);

	if (NightDuration <= 0.0f || ElapsedNightMinutes >= NightDuration)
	{
		return 0.0f;
	}

	const float FadeDuration = FMath::Min(
		FadeDurationMinutes,
		NightDuration * 0.5f);

	if (FadeDuration <= 0.0f)
	{
		return 1.0f;
	}

	if (ElapsedNightMinutes < FadeDuration)
	{
		return ElapsedNightMinutes / FadeDuration;
	}

	if (ElapsedNightMinutes > NightDuration - FadeDuration)
	{
		return (NightDuration - ElapsedNightMinutes) / FadeDuration;
	}

	return 1.0f;
}
