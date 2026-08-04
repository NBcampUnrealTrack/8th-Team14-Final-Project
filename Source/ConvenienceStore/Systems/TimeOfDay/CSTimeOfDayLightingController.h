#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSTimeOfDayLightingController.generated.h"

class ADirectionalLight;
class ACSStoreGameState;
class UCurveFloat;

// 편의점 게임 시각에 맞춰 태양 방향을 회전시키는 레벨 배치용 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSTimeOfDayLightingController : public AActor
{
	GENERATED_BODY()

public:
	ACSTimeOfDayLightingController();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting")
	TObjectPtr<ADirectionalLight> SunLight;

	// 0~1 하루 비율을 입력받아 태양 Pitch 각도를 반환하는 커브입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting")
	TObjectPtr<UCurveFloat> SunPitchCurve;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting")
	float SunYaw = -90.0f;

	UFUNCTION()
	void HandleGameTimeChanged(float NewGameMinutes);

private:
	TObjectPtr<ACSStoreGameState> StoreGameState;
};
