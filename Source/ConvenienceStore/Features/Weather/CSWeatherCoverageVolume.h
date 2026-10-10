#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSWeatherCoverageVolume.generated.h"

class UCSWeatherCoverageComponent;

// 캐릭터 주위 비·눈 범위 밖에서도 보이는 구간(정문 앞, 뒷마당 등)에 레벨 단위로 배치할 날씨 커버 액터
// 박스 윗면을 캐릭터 주위 파티클 생성 높이에 맞추고 회전은 Yaw만 반영
UCLASS()
class CONVENIENCESTORE_API ACSWeatherCoverageVolume : public AActor
{
	GENERATED_BODY()

public:
	ACSWeatherCoverageVolume();

protected:
	// 비·눈 파티클을 월드에 고정해 재생할 범위를 지정하는 루트 박스 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCSWeatherCoverageComponent> CoverageBox;
};
