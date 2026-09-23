#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSPlayerWakePoint.generated.h"

class UArrowComponent;

// 원점은 플레이어 Capsule 중심, Arrow는 깨어난 뒤 바라볼 방향입니다.
UCLASS()
class CONVENIENCESTORE_API ACSPlayerWakePoint : public AActor
{
	GENERATED_BODY()
public:
	ACSPlayerWakePoint();
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wake Point")
	TObjectPtr<UArrowComponent> WakeDirection;
};
