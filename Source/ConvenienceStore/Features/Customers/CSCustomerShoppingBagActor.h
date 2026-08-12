#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSCustomerShoppingBagActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;

// 계산을 완료한 고객이 들고 나가는 내용물이 보이지 않는 봉투 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSCustomerShoppingBagActor : public AActor
{
	GENERATED_BODY()

public:
	ACSCustomerShoppingBagActor();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShoppingBagMesh;
};
