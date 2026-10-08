#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSWeatherBlockerVolume.generated.h"

class UCSWeatherBlockerComponent;

// 스태틱 메시 건물처럼 BP가 없는 건물에 레벨 단위로 배치할 날씨 블로커 액터
// 회전은 Yaw만 반영하고 크기는 Box Extent나 액터 Scale로 조절
UCLASS()
class CONVENIENCESTORE_API ACSWeatherBlockerVolume : public AActor
{
	GENERATED_BODY()

public:
	ACSWeatherBlockerVolume();

protected:
	// 비·눈 파티클을 소멸시킬 범위를 지정하는 루트 박스 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCSWeatherBlockerComponent> BlockerBox;
};
