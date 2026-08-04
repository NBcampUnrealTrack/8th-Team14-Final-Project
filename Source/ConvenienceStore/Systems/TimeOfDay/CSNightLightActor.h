#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSNightLightActor.generated.h"

class ACSStoreGameState;
class UPointLightComponent;
class USceneComponent;

// 편의점의 AM/PM 게임 시각에 맞춰 점등되는 재사용 가능한 야간 조명입니다.
UCLASS()
class CONVENIENCESTORE_API ACSNightLightActor : public AActor
{
	GENERATED_BODY()

public:
	ACSNightLightActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> NightLight;

	// 점등 시작 시각입니다. 1080은 06:00 PM입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Night Light", meta = (ClampMin = "0", ClampMax = "1439"))
	float NightStartMinutes = 1080.0f;

	// 소등 시각입니다. 360은 06:00 AM입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Night Light", meta = (ClampMin = "0", ClampMax = "1439"))
	float NightEndMinutes = 360.0f;

	// 점등과 소등에 사용하는 게임 시간 기준 페이드 길이입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Night Light", meta = (ClampMin = "0"))
	float FadeDurationMinutes = 15.0f;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Night Light", meta = (ClampMin = "0"))
	// 최대 점등 밝기입니다.
	float NightLightIntensity = 3000.0f;

	// 점등할 때 사용할 조명 색입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Night Light")
	FLinearColor NightLightColor = FLinearColor(1.0f, 0.75f, 0.45f);

	UFUNCTION()
	void HandleGameTimeChanged(float NewGameMinutes);

private:
	float GetNightBrightness(float GameMinutes) const;

	TObjectPtr<ACSStoreGameState> StoreGameState;
};
