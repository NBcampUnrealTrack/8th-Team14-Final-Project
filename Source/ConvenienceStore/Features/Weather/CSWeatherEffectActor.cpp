#include "Features/Weather/CSWeatherEffectActor.h"

#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"

ACSWeatherEffectActor::ACSWeatherEffectActor()
{
	// 색감 페이드 중에만 Tick을 활성화하도록 시작 시 비활성화
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	WeatherPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("WeatherPostProcess"));
	WeatherPostProcess->SetupAttachment(SceneRoot);
	// 액터 위치와 관계없이 화면 전체에 색감을 적용
	WeatherPostProcess->bUnbound = true;
	// Priority 0인 레벨 PostProcessVolume보다 겹치는 항목을 우선 적용
	WeatherPostProcess->Priority = 1.0f;
	// ActivatePostProcess 호출 전에는 화면에 영향을 주지 않도록 0으로 설정
	WeatherPostProcess->BlendWeight = 0.0f;
}

void ACSWeatherEffectActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 컴포넌트가 없거나 페이드 시간이 0이면 Tick을 중지
	if (!IsValid(WeatherPostProcess) || PostProcessFadeSeconds <= KINDA_SMALL_NUMBER)
	{
		SetActorTickEnabled(false);
		return;
	}

	// 초당 1 / PostProcessFadeSeconds만큼 목표 가중치로 이동
	// 페이드 중 실내외가 바뀌면 현재 값에서 새 목표로 이어서 이동
	const float Target = GetTargetPostProcessWeight();
	const float MaxStep = DeltaSeconds / PostProcessFadeSeconds;
	const float Current = WeatherPostProcess->BlendWeight;
	WeatherPostProcess->BlendWeight = Current + FMath::Clamp(Target - Current, -MaxStep, MaxStep);

	// 목표 가중치에 도달하면 값을 고정하고 Tick을 중지
	if (FMath::IsNearlyEqual(WeatherPostProcess->BlendWeight, Target))
	{
		WeatherPostProcess->BlendWeight = Target;
		SetActorTickEnabled(false);
	}
}

void ACSWeatherEffectActor::ActivatePostProcess(bool bFade)
{
	// 색감 활성화 상태를 기록하고 현재 실내외 상태의 가중치를 적용
	bPostProcessActivated = true;
	RefreshPostProcessWeight(bFade);
}

float ACSWeatherEffectActor::GetTargetPostProcessWeight() const
{
	// 활성화 전이거나 종료 중이면 색감을 적용하지 않음
	if (!bPostProcessActivated || bStopRequested)
	{
		return 0.0f;
	}

	// 파티클 표시 상태를 실외 여부로 사용해 가중치를 반환
	return bLocalEffectActive ? OutdoorPostProcessWeight : IndoorPostProcessWeight;
}

void ACSWeatherEffectActor::RefreshPostProcessWeight(bool bFade)
{
	if (!IsValid(WeatherPostProcess))
	{
		return;
	}

	// 페이드를 요청하지 않았거나 페이드 시간이 0이면 목표 가중치를 즉시 적용
	const float Target = GetTargetPostProcessWeight();
	if (!bFade || PostProcessFadeSeconds <= KINDA_SMALL_NUMBER)
	{
		WeatherPostProcess->BlendWeight = Target;
		SetActorTickEnabled(false);
		return;
	}

	// 현재 값이 목표와 다를 때만 Tick을 활성화해 페이드를 처리
	SetActorTickEnabled(!FMath::IsNearlyEqual(WeatherPostProcess->BlendWeight, Target));
}

void ACSWeatherEffectActor::StartEffect_Implementation()
{
	// 기본 구현은 비어 있습니다. 나이아가라 Activate는 상속 BP가 합니다.
}

void ACSWeatherEffectActor::StopEffect_Implementation()
{
	if (bStopRequested)
	{
		// 이미 종료 절차가 시작되었습니다. Destroy를 다시 부르지 않습니다.
		return;
	}
	bStopRequested = true;

	// 정산 UI를 닫는 순간이 곧 다음 날이므로 잦아들 시간을 두지 않습니다.
	// 액터와 함께 나이아가라 컴포넌트가 지워지면서 남은 파티클도 같은 프레임에 사라집니다.
	Destroy();
}

void ACSWeatherEffectActor::SetLocalEffectActive_Implementation(bool bActive)
{
	// 기존 상태와 같으면 추가 처리 없이 종료
	if (bLocalEffectActive == bActive)
	{
		return;
	}
	// 요청받은 표시 상태를 저장
	bLocalEffectActive = bActive;

	// 액터가 가진 나이아가라 컴포넌트를 수집
	TArray<UNiagaraComponent*> NiagaraComponents;
	GetComponents<UNiagaraComponent>(NiagaraComponents);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		// 유효하지 않은 컴포넌트는 제외
		if (!IsValid(NiagaraComponent))
		{
			continue;
		}

		// 파티클 재생을 다시 시작하지 않고 표시 여부만 변경
		NiagaraComponent->SetVisibility(bActive);
	}

	// 색감은 끄지 않고 실외/실내 가중치로 전환
	// 문을 드나들 때 색이 한 번에 바뀌지 않도록 항상 페이드로 처리
	RefreshPostProcessWeight(true);
}
