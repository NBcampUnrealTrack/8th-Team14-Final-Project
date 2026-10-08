#include "Features/Weather/CSWeatherBlockerComponent.h"

#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Systems/Weather/CSWeatherSubsystem.h"

UCSWeatherBlockerComponent::UCSWeatherBlockerComponent()
{
	// CDO 생성 시 렌더·물리 상태 갱신이 실행되지 않도록 값만 설정
	// SetBoxExtent는 등록 전 컴포넌트에서 갱신을 실행해 엔진 ensure를 발생
	InitBoxExtent(FVector(500.0f, 500.0f, 500.0f));

	// 판정 전용 영역이므로 충돌과 오버랩을 사용하지 않도록 설정
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);

	// 건물 위에 배치해도 NavMesh를 변경하지 않도록 설정
	SetCanEverAffectNavigation(false);

	// 실내 판정 영역(초록)과 구분하도록 에디터 표시 색을 파랑으로 설정
	ShapeColor = FColor::Blue;
}

void UCSWeatherBlockerComponent::BeginPlay()
{
	Super::BeginPlay();

	// 등록 시 서브시스템이 현재 날씨 이펙트에 블로커 목록을 다시 전달
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->RegisterWeatherBlocker(this);
		}
	}
}

void UCSWeatherBlockerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UCSWeatherSubsystem>())
		{
			WeatherSubsystem->UnregisterWeatherBlocker(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
