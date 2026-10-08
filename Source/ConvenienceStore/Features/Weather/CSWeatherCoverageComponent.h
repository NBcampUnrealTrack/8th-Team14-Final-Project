#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "CSWeatherCoverageComponent.generated.h"

// 캐릭터 주위 비·눈 범위 밖까지 같은 날씨 파티클을 월드에 고정해 재생할 영역
// 박스 윗면이 파티클 생성 높이이고 박스 XY 크기가 생성 범위
// 캐릭터 주위 파티클은 이 박스 안에서 생성된 입자만 소멸
// 충돌·오버랩·내비게이션에는 영향을 주지 않도록 설정
UCLASS(ClassGroup = (Weather), meta = (BlueprintSpawnableComponent))
class CONVENIENCESTORE_API UCSWeatherCoverageComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UCSWeatherCoverageComponent();

protected:
	// 날씨 서브시스템에 커버 영역을 등록
	virtual void BeginPlay() override;

	// 날씨 서브시스템에서 커버 영역 등록을 해제
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
