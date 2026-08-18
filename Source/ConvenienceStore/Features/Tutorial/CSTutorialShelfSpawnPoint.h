#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSTutorialShelfSpawnPoint.generated.h"

// 튜토리얼 DeliveryArrived 스텝에서 배송 선반을 스폰할 위치를 표시하는 마커 액터
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSTutorialShelfSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ACSTutorialShelfSpawnPoint();

	// 마커 반짝임(HighlightMarkerTag)을 쓰고 싶을 때 DT에 그대로 입력할 기본 태그
	static const FName DefaultSpawnPointTag;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;
};
