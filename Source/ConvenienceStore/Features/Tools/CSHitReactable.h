#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSHitReactable.generated.h"

UINTERFACE(BlueprintType)
class CONVENIENCESTORE_API UCSHitReactable : public UInterface
{
	GENERATED_BODY()
};

// 투사체, 함정 등 외부 효과에 피격 반응을 제공하는 액터가 구현하는 인터페이스입니다.
class CONVENIENCESTORE_API ICSHitReactable
{
	GENERATED_BODY()

public:
	// 피격 위치와 공격 진행 방향을 받아 액터별 넉백 처리 방식을 실행합니다.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	void ApplyMagicHit(FVector HitLocation, FVector ImpactDirection, float KnockbackStrength);
};
