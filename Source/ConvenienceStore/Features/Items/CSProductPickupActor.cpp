#include "Features/Items/CSProductPickupActor.h"

#include "Components/StaticMeshComponent.h"
#include "Features/Player/CSPlayerCharacter.h"

ACSProductPickupActor::ACSProductPickupActor()
{
	PrimaryActorTick.bCanEverTick = false;

	ProductMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProductMesh"));
	SetRootComponent(ProductMesh);
	ProductMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSProductPickupActor::Interact_Implementation(AActor* Interactor)
{
	if (ACSPlayerCharacter* PlayerCharacter = Cast<ACSPlayerCharacter>(Interactor))
	{
		PlayerCharacter->TryPickUp(this);
	}
}

FText ACSProductPickupActor::GetInteractionText_Implementation() const
{
	return FText::Format(
		NSLOCTEXT("Interaction", "PickUp", "{0} 집기"),
		DisplayName.IsEmpty() ? FText::FromName(ProductId) : DisplayName);
}
