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

	// 야간에 태양 반대편 하늘에서 비출 달빛 Directional Light입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting|Moon")
	TObjectPtr<ADirectionalLight> MoonLight;

	// 0~1 하루 비율을 입력받아 태양 Pitch 각도를 반환하는 커브입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting")
	TObjectPtr<UCurveFloat> SunPitchCurve;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting")
	float SunYaw = -90.0f;

	// 달빛 점등 시작 시각입니다. 1080은 18:00입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting|Moon",
		meta = (ClampMin = "0", ClampMax = "1439"))
	float MoonStartMinutes = 1080.0f;

	// 달빛 소등 시각입니다. 0은 00:00입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting|Moon",
		meta = (ClampMin = "0", ClampMax = "1439"))
	float MoonEndMinutes = 0.0f;

	// 점등과 소등에 사용할 게임 시간 기준 페이드 길이입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting|Moon",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoonFadeDurationMinutes = 20.0f;

	// 달빛의 최대 밝기입니다. 레벨의 노출값에 맞춰 조정합니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting|Moon",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoonLightIntensity = 0.5f;

	// 밤 분위기를 위한 약한 청색 계열 달빛 색상입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Lighting|Moon")
	FLinearColor MoonLightColor = FLinearColor(0.45f, 0.55f, 1.0f);

	UFUNCTION()
	void HandleGameTimeChanged(float NewGameMinutes);

private:
	// 18:00~00:00 구간에서만 달빛 밝기를 0~1 범위로 계산합니다.
	float GetMoonBrightness(float GameMinutes) const;

	TObjectPtr<ACSStoreGameState> StoreGameState;
};
