#include "Features/Weather/CSWeatherEffectActor.h"

#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"

ACSWeatherEffectActor::ACSWeatherEffectActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
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
}
