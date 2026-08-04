#include "Systems/TimeOfDay/CSTimeOfDayLightingController.h"

#include "Curves/CurveFloat.h"
#include "Engine/DirectionalLight.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

ACSTimeOfDayLightingController::ACSTimeOfDayLightingController()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACSTimeOfDayLightingController::BeginPlay()
{
	Super::BeginPlay();

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnGameTimeChanged.AddDynamic(
			this, &ACSTimeOfDayLightingController::HandleGameTimeChanged);
		HandleGameTimeChanged(StoreGameState->CurrentGameMinutes);
	}
}

void ACSTimeOfDayLightingController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StoreGameState != nullptr)
	{
		StoreGameState->OnGameTimeChanged.RemoveDynamic(
			this, &ACSTimeOfDayLightingController::HandleGameTimeChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void ACSTimeOfDayLightingController::HandleGameTimeChanged(float NewGameMinutes)
{
	if (SunLight == nullptr)
	{
		return;
	}

	const float NormalizedTime = FMath::Clamp(NewGameMinutes / 1440.0f, 0.0f, 1.0f);
	const float SunPitch = SunPitchCurve != nullptr
		? SunPitchCurve->GetFloatValue(NormalizedTime)
		// 음수 Pitch가 지면을 향하도록 하여 06시 일출, 정오, 18시 일몰 방향을 계산합니다.
		: 90.0f - NormalizedTime * 360.0f;

	SunLight->SetActorRotation(FRotator(SunPitch, SunYaw, 0.0f));
}
