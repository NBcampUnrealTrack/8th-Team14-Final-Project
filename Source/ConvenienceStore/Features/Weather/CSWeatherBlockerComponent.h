#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "CSWeatherBlockerComponent.generated.h"

// 박스 안에 들어온 비·눈 파티클을 나이아가라에서 소멸시킬 영역
// 건물 BP에 붙이거나 ACSWeatherBlockerVolume으로 레벨에 배치
// 충돌·오버랩·내비게이션에는 영향을 주지 않도록 설정
UCLASS(ClassGroup = (Weather), meta = (BlueprintSpawnableComponent))
class CONVENIENCESTORE_API UCSWeatherBlockerComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UCSWeatherBlockerComponent();

protected:
	// 날씨 서브시스템에 블로커를 등록
	virtual void BeginPlay() override;

	// 날씨 서브시스템에서 블로커 등록을 해제
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
