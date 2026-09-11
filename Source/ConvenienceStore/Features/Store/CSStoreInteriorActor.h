#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSStoreInteriorActor.generated.h"

class UBoxComponent;

// 매장 실내 영역을 표시하는 액터. 레벨에서 매장 벽 안쪽에 맞춰 박스 크기·회전을 조정해 배치한다
// 사전 배치 쓰레기까지 포함해 "매장 안에 보이는 것"만 청결도 판정에 넣으려고
// CSCustomerRuntimeSubsystem이 이 영역으로 쓰레기 위치를 걸러내는 데 쓴다
// 레벨에 하나만 배치하는 것을 전제로 한다 - 여러 개 배치해도 서브시스템은 처음 찾은 것만 쓴다
UCLASS()
class CONVENIENCESTORE_API ACSStoreInteriorActor : public AActor
{
	GENERATED_BODY()

public:
	ACSStoreInteriorActor();

	// Location이 이 영역(박스) 안에 있는지 확인한다. 박스가 회전되어 배치되어도 정확히 판정한다
	bool EncompassesPoint(const FVector& Location) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> InteriorBox;
};
