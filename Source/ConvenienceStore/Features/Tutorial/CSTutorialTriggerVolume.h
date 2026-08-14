#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSTutorialTypes.h"
#include "CSTutorialTriggerVolume.generated.h"

class UBoxComponent;

// 플레이어가 이 볼륨에 들어오면 지정한 튜토리얼 스텝을 완료 처리하는 트리거
// 매장 입구에 배치해 이동하기 스텝을 완료시키는 용도
// 레벨에 여러 개를 배치해 TargetStepId만 다르게 지정하면 재사용할 수 있음
UCLASS()
class CONVENIENCESTORE_API ACSTutorialTriggerVolume : public AActor
{
	GENERATED_BODY()

public:
	ACSTutorialTriggerVolume();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	// 이 볼륨에 들어왔을 때 완료 처리할 스텝
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	ECSTutorialStepId TargetStepId = ECSTutorialStepId::MoveToStore;

	UFUNCTION()
	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
};
