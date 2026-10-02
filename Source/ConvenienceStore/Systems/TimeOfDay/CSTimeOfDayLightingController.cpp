#include "Systems/TimeOfDay/CSTimeOfDayLightingController.h"

#include "Curves/CurveFloat.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
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

void ACSTimeOfDayLightingController::SetMoonIntensityScale(float NewScale)
{
	MoonIntensityScale = FMath::Max(0.0f, NewScale);

	// 다음 게임 분을 기다리지 않고 현재 시각의 달빛에 반영
	// BeginPlay 전이면 값만 저장하고 BeginPlay의 첫 계산에서 반영
	if (StoreGameState != nullptr)
	{
		HandleGameTimeChanged(StoreGameState->CurrentGameMinutes);
	}
}

void ACSTimeOfDayLightingController::HandleGameTimeChanged(float NewGameMinutes)
{
	// 다음 날 새벽 시간은 24시간 주기로 다시 계산하여 02:00 조명도 자연스럽게 적용합니다.
	const float MinutesInDay = 1440.0f;
	const float TimeOfDayMinutes = FMath::Fmod(
		FMath::Max(0.0f, NewGameMinutes),
		MinutesInDay);
	const float NormalizedTime = TimeOfDayMinutes / MinutesInDay;
	const float SunPitch = SunPitchCurve != nullptr
		? SunPitchCurve->GetFloatValue(NormalizedTime)
		// 음수 Pitch가 지면을 향하도록 하여 06시 일출, 정오, 18시 일몰 방향을 계산합니다.
		: 90.0f - NormalizedTime * 360.0f;

	if (IsValid(SunLight))
	{
		SunLight->SetActorRotation(FRotator(SunPitch, SunYaw, 0.0f));
	}

	const float MoonBrightness = GetMoonBrightness(TimeOfDayMinutes);
	if (IsValid(MoonLight))
	{
		// 달은 태양보다 12시간 뒤의 높이를 사용하되, 해가 뜨는 쪽과 같은 수평 방향에서 떠오르게 합니다.
		MoonLight->SetActorRotation(FRotator(SunPitch + 180.0f, SunYaw, 0.0f));
		MoonLight->SetLightColor(MoonLightColor);
		MoonLight->GetLightComponent()->SetIntensity(MoonLightIntensity * MoonBrightness * MoonIntensityScale);
		MoonLight->SetActorHiddenInGame(MoonBrightness <= KINDA_SMALL_NUMBER);
	}

	// 투명 재질·Volumetric Fog에는 Directional Light 하나만 적용되므로,
	// 달빛이 켜진 시간에는 달을 우선하고 그 외 시간에는 태양을 우선합니다.
	const bool bMoonIsActive = IsValid(MoonLight) && MoonBrightness > KINDA_SMALL_NUMBER;
	if (IsValid(SunLight))
	{
		if (UDirectionalLightComponent* SunComponent =
			Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
		{
			SunComponent->SetForwardShadingPriority(bMoonIsActive ? 0 : 1);
		}
	}
	if (IsValid(MoonLight))
	{
		if (UDirectionalLightComponent* MoonComponent =
			Cast<UDirectionalLightComponent>(MoonLight->GetLightComponent()))
		{
			MoonComponent->SetForwardShadingPriority(bMoonIsActive ? 1 : 0);
		}
	}
}

float ACSTimeOfDayLightingController::GetMoonBrightness(float GameMinutes) const
{
	constexpr float MinutesInDay = 1440.0f;
	const float CurrentTime = FMath::Fmod(FMath::Max(0.0f, GameMinutes), MinutesInDay);
	const float MoonDuration = FMath::Fmod(
		MoonEndMinutes - MoonStartMinutes + MinutesInDay,
		MinutesInDay);
	const float ElapsedMoonMinutes = FMath::Fmod(
		CurrentTime - MoonStartMinutes + MinutesInDay,
		MinutesInDay);

	if (MoonDuration <= 0.0f || ElapsedMoonMinutes >= MoonDuration)
	{
		return 0.0f;
	}

	const float FadeDuration = FMath::Min(
		MoonFadeDurationMinutes,
		MoonDuration * 0.5f);
	if (FadeDuration <= 0.0f)
	{
		return 1.0f;
	}

	if (ElapsedMoonMinutes < FadeDuration)
	{
		return ElapsedMoonMinutes / FadeDuration;
	}

	if (ElapsedMoonMinutes > MoonDuration - FadeDuration)
	{
		return (MoonDuration - ElapsedMoonMinutes) / FadeDuration;
	}

	return 1.0f;
}
