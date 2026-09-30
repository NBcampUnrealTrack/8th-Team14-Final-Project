#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSFairyHouse.generated.h"

class UArrowComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSFairyHouse : public AActor
{
	GENERATED_BODY()
public:
	ACSFairyHouse();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy House")
	TObjectPtr<UStaticMeshComponent> HouseMesh;
};

// 매장에 하나만 배치해 주세요. 화살표의 월드 변환을 요정의 집 설치 위치로 사용합니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSFairyHousePlacementPoint : public AActor
{
	GENERATED_BODY()
public:
	ACSFairyHousePlacementPoint();
	// 블루프린트에서 이 컴포넌트의 위치, 회전과 크기를 조절해 주세요.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy House")
	TObjectPtr<UArrowComponent> PlacementPoint;
};
