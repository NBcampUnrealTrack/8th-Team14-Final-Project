#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryOrderIndicatorActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;

// 배달 주문의 터미널 또는 목적지를 가리키는 반복 이동 화살표입니다.
UCLASS()
class CONVENIENCESTORE_API ACSDeliveryOrderIndicatorActor : public AActor
{
	GENERATED_BODY()

public:
	ACSDeliveryOrderIndicatorActor();
	virtual void Tick(float DeltaSeconds) override;

	// 주문 상태에 맞춰 화살표를 표시하거나 숨기고, 표시할 때 애니메이션 시간을 초기화합니다.
	void SetIndicatorVisible(bool bShouldShow);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// BP_DeliveryOrderIndicator에서 큰 화살표 Static Mesh를 지정합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ArrowMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Indicator|Animation")
	float FloatAmplitude = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Indicator|Animation")
	float FloatSpeed = 1.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Indicator|Animation")
	float RotationSpeedDegreesPerSecond = 45.0f;

private:
	FVector InitialArrowRelativeLocation = FVector::ZeroVector;
	float AnimationElapsedSeconds = 0.0f;
};
