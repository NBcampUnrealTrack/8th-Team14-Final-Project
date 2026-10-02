#include "Features/Weather/CSWeatherEnvironmentController.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Features/Weather/CSWeatherEffectActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Systems/TimeOfDay/CSTimeOfDayLightingController.h"
#include "Systems/Weather/CSWeatherSubsystem.h"

ACSWeatherEnvironmentController::ACSWeatherEnvironmentController()
{
	// 날씨 보간 중에만 Tick을 활성화하도록 시작 시 비활성화
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void ACSWeatherEnvironmentController::BeginPlay()
{
	Super::BeginPlay();

	// 지정된 환경 액터의 현재 값을 원래 값으로 저장
	// 낮밤 컨트롤러가 변경하지 않는 속성만 저장해 BeginPlay 순서와 관계없이 같은 값을 유지
	CaptureBaseline();

	UCSWeatherSubsystem* Weather = GetWorld()->GetSubsystem<UCSWeatherSubsystem>();
	if (Weather == nullptr)
	{
		return;
	}

	// 날씨 변경 알림을 구독하고 해제할 때 사용할 서브시스템을 보관
	WeatherSubsystem = Weather;
	Weather->OnWeatherChanged.AddDynamic(this, &ACSWeatherEnvironmentController::HandleWeatherChanged);

	// 서브시스템은 OnWorldBeginPlay에서 이미 첫날 날씨와 이펙트 액터를 적용했으므로 현재 날씨를 즉시 반영
	HandleWeatherChanged(Weather->GetCurrentWeather(), ECSWeatherType::None);
}

void ACSWeatherEnvironmentController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCSWeatherSubsystem* Weather = WeatherSubsystem.Get())
	{
		Weather->OnWeatherChanged.RemoveDynamic(this, &ACSWeatherEnvironmentController::HandleWeatherChanged);
	}

	// 컨트롤러 제거나 레벨 종료 시 환경 액터를 원래 값으로 복구
	if (bBaselineCaptured)
	{
		ApplyProfile(FCSWeatherEnvironmentProfile());
	}

	// 구름 컴포넌트를 MID로 교체하기 전의 머티리얼로 복구
	if (CloudMID != nullptr)
	{
		if (UVolumetricCloudComponent* CloudComponent = GetCloudComponent())
		{
			CloudComponent->SetMaterial(OriginalCloudMaterial);
		}

		CloudMID = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ACSWeatherEnvironmentController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 경과 시간 비율로 보간 위치를 계산하고 보간 시간이 0이면 목표 프로필을 즉시 적용
	BlendElapsed += DeltaSeconds;
	const float Alpha = BlendSeconds > KINDA_SMALL_NUMBER
		? FMath::Clamp(BlendElapsed / BlendSeconds, 0.0f, 1.0f)
		: 1.0f;
	ApplyProfile(FCSWeatherEnvironmentProfile::Lerp(FromProfile, ToProfile, Alpha));

	// 목표에 도달하면 Tick을 중지
	if (Alpha >= 1.0f)
	{
		SetActorTickEnabled(false);
	}
}

void ACSWeatherEnvironmentController::HandleWeatherChanged(ECSWeatherType NewWeather, ECSWeatherType PreviousWeather)
{
	const UCSWeatherSubsystem* Weather = WeatherSubsystem.Get();
	const int32 BusinessDay = Weather != nullptr ? Weather->GetCurrentBusinessDay() : INDEX_NONE;

	// 날씨가 같아도 매일 알림이 오므로 같은 날씨와 영업일이면 무시
	if (NewWeather == AppliedWeather && BusinessDay == AppliedBusinessDay)
	{
		return;
	}

	// 같은 영업일 안의 변경(날씨의 돌, 디버그)은 보간
	// 첫 적용, 영업일 전환, 세이브 로드는 정산 UI나 로딩 뒤이므로 즉시 적용
	const bool bBlend = AppliedWeather != ECSWeatherType::None
		&& BusinessDay == AppliedBusinessDay
		&& BlendSeconds > KINDA_SMALL_NUMBER;

	AppliedWeather = NewWeather;
	AppliedBusinessDay = BusinessDay;
	ToProfile = GetTargetProfile();

	if (!bBlend)
	{
		ApplyProfile(ToProfile);
		SetActorTickEnabled(false);
		return;
	}

	// 보간 중이었다면 현재 화면 값에서 이어서 보간
	// 이전 이펙트 액터가 제거되어도 영향이 없도록 프로필 값을 복사해 사용
	FromProfile = AppliedProfile;
	BlendElapsed = 0.0f;
	SetActorTickEnabled(true);
}

FCSWeatherEnvironmentProfile ACSWeatherEnvironmentController::GetTargetProfile() const
{
	// 서브시스템이 이펙트 액터를 스폰한 뒤 알림을 방송하므로 새 날씨의 액터를 조회
	const UCSWeatherSubsystem* Weather = WeatherSubsystem.Get();
	const ACSWeatherEffectActor* EffectActor = Weather != nullptr ? Weather->GetActiveEffectActor() : nullptr;

	// 맑음처럼 이펙트 액터가 없으면 원래 값을 유지하는 기본 프로필 사용
	FCSWeatherEnvironmentProfile Profile = IsValid(EffectActor)
		? EffectActor->GetEnvironmentProfile()
		: FCSWeatherEnvironmentProfile();

	// 덮어쓰지 않은 구름 값이 기본값 0에서 보간되지 않도록 원래 값으로 채움
	ResolveCloudValues(Profile);
	return Profile;
}

void ACSWeatherEnvironmentController::ResolveCloudValues(FCSWeatherEnvironmentProfile& Profile) const
{
	if (!Profile.bOverrideCloudCoverage)
	{
		Profile.bOverrideCloudCoverage = true;
		Profile.CloudCoverage = Baseline.CloudCoverage;
	}

	if (!Profile.bOverrideCloudDensity)
	{
		Profile.bOverrideCloudDensity = true;
		Profile.CloudDensity = Baseline.CloudDensity;
	}
}

UVolumetricCloudComponent* ACSWeatherEnvironmentController::GetCloudComponent() const
{
	// AVolumetricCloud는 C++용 컴포넌트 getter가 없으므로 컴포넌트 검색으로 조회
	return IsValid(VolumetricCloud) ? VolumetricCloud->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
}

void ACSWeatherEnvironmentController::CaptureBaseline()
{
	if (IsValid(SunLight) && SunLight->GetLightComponent() != nullptr)
	{
		Baseline.SunIntensity = SunLight->GetLightComponent()->Intensity;
		Baseline.SunColor = SunLight->GetLightComponent()->GetLightColor();
	}

	if (IsValid(SkyLight) && SkyLight->GetLightComponent() != nullptr)
	{
		Baseline.SkyLightIntensity = SkyLight->GetLightComponent()->Intensity;
	}

	if (IsValid(SkyAtmosphere) && SkyAtmosphere->GetComponent() != nullptr)
	{
		Baseline.RayleighScatteringScale = SkyAtmosphere->GetComponent()->RayleighScatteringScale;
		Baseline.MieScatteringScale = SkyAtmosphere->GetComponent()->MieScatteringScale;
	}

	if (IsValid(HeightFog) && HeightFog->GetComponent() != nullptr)
	{
		Baseline.FogDensity = HeightFog->GetComponent()->FogDensity;
		Baseline.FogInscatteringColor = HeightFog->GetComponent()->FogInscatteringLuminance;
		Baseline.FogSkyAtmosphereContribution = HeightFog->GetComponent()->SkyAtmosphereAmbientContributionColorScale;
	}

	// 구름 머티리얼을 런타임 MID로 교체하고 원래 값을 저장
	// 머티리얼 에셋은 수정하지 않고 EndPlay에서 원래 머티리얼로 복구
	UVolumetricCloudComponent* CloudComponent = GetCloudComponent();
	UMaterialInterface* CloudMaterial = CloudComponent != nullptr ? CloudComponent->GetMaterial() : nullptr;
	if (CloudMaterial != nullptr)
	{
		OriginalCloudMaterial = CloudMaterial;
		CloudMID = UMaterialInstanceDynamic::Create(CloudMaterial, this);
		CloudComponent->SetMaterial(CloudMID);

		// 인스턴스가 덮어쓰지 않은 파라미터는 부모 머티리얼 값을 조회
		// 파라미터가 없는 머티리얼이면 값 변경이 반영되지 않으므로 경고 로그를 출력
		const bool bHasCoverage = CloudMID->GetScalarParameterValue(CloudCoverageParameterName, Baseline.CloudCoverage);
		const bool bHasDensity = CloudMID->GetScalarParameterValue(CloudDensityParameterName, Baseline.CloudDensity);
		if (!bHasCoverage || !bHasDensity)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[WeatherEnvironment] 구름 머티리얼 %s에서 파라미터를 찾지 못했습니다. Coverage(%s): %d, Density(%s): %d"),
				*GetNameSafe(CloudMaterial),
				*CloudCoverageParameterName.ToString(), static_cast<int32>(bHasCoverage),
				*CloudDensityParameterName.ToString(), static_cast<int32>(bHasDensity));
		}
	}

	bBaselineCaptured = true;
}

void ACSWeatherEnvironmentController::ApplyProfile(const FCSWeatherEnvironmentProfile& Profile)
{
	// 보간 중 날씨가 바뀌면 시작점으로 사용하도록 적용한 프로필을 기록
	AppliedProfile = Profile;

	// 지정하지 않은 액터는 건너뛰어 환경 액터가 없는 맵에서도 동작하도록 처리
	if (IsValid(SunLight))
	{
		if (ULightComponent* SunComponent = SunLight->GetLightComponent())
		{
			SunComponent->SetIntensity(Baseline.SunIntensity * Profile.SunIntensityScale);
			SunComponent->SetLightColor(Baseline.SunColor * Profile.SunColorTint);
		}
	}

	if (IsValid(SkyLight))
	{
		// Real Time Capture를 사용하므로 재캡처 호출 없이 밝기만 변경
		if (USkyLightComponent* SkyLightComponent = SkyLight->GetLightComponent())
		{
			SkyLightComponent->SetIntensity(Baseline.SkyLightIntensity * Profile.SkyLightIntensityScale);
		}
	}

	if (IsValid(SkyAtmosphere))
	{
		if (USkyAtmosphereComponent* AtmosphereComponent = SkyAtmosphere->GetComponent())
		{
			AtmosphereComponent->SetRayleighScatteringScale(
				Baseline.RayleighScatteringScale * Profile.RayleighScatteringScale);
			AtmosphereComponent->SetMieScatteringScale(
				Baseline.MieScatteringScale * Profile.MieScatteringScale);
		}
	}

	if (IsValid(HeightFog))
	{
		if (UExponentialHeightFogComponent* FogComponent = HeightFog->GetComponent())
		{
			FogComponent->SetFogDensity(Baseline.FogDensity * Profile.FogDensityScale);

			// 안개 색은 자체 색과 Sky Atmosphere 하늘빛의 합이므로 두 값에 같은 틴트를 적용
			// 자체 색이 검정인 맵에서는 하늘빛 배율로 안개 색을 변경
			FogComponent->SetFogInscatteringColor(Baseline.FogInscatteringColor * Profile.FogInscatteringTint);
			FogComponent->SetSkyAtmosphereAmbientContributionColorScale(
				Baseline.FogSkyAtmosphereContribution * Profile.FogInscatteringTint);
		}
	}

	// 구름 머티리얼 파라미터만 변경하고 덮어쓰지 않은 값은 원래 값을 사용
	// 머티리얼 교체 없이 값만 갱신하므로 보간 중 매 틱 호출
	if (CloudMID != nullptr)
	{
		CloudMID->SetScalarParameterValue(CloudCoverageParameterName,
			Profile.bOverrideCloudCoverage ? Profile.CloudCoverage : Baseline.CloudCoverage);
		CloudMID->SetScalarParameterValue(CloudDensityParameterName,
			Profile.bOverrideCloudDensity ? Profile.CloudDensity : Baseline.CloudDensity);
	}

	// 달빛은 낮밤 컨트롤러가 게임 시각마다 덮어쓰므로 배수만 전달
	// 배수가 바뀔 때만 전달해 보간 중 불필요한 달빛 재계산을 방지
	if (IsValid(TimeOfDayController) && !FMath::IsNearlyEqual(SentMoonIntensityScale, Profile.MoonIntensityScale))
	{
		SentMoonIntensityScale = Profile.MoonIntensityScale;
		TimeOfDayController->SetMoonIntensityScale(Profile.MoonIntensityScale);
	}
}
