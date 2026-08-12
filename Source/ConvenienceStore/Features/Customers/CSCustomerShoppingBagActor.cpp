#include "Features/Customers/CSCustomerShoppingBagActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

ACSCustomerShoppingBagActor::ACSCustomerShoppingBagActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ShoppingBagMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShoppingBagMesh"));
	ShoppingBagMesh->SetupAttachment(SceneRoot);
	ShoppingBagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
