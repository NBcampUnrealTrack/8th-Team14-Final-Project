#include "Features/Trash/CSTrashActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Features/Player/CSPlayerCharacter.h"

ACSTrashActor::ACSTrashActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TrashMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrashMesh"));
	TrashMesh->SetupAttachment(SceneRoot);
	// E키 조준(라인트레이스)이 이 메시를 인식하도록 Visibility 채널을 막아둠
	TrashMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSTrashActor::Interact_Implementation(AActor* Interactor)
{
	if (ACSPlayerCharacter* PlayerCharacter = Cast<ACSPlayerCharacter>(Interactor))
	{
		PlayerCharacter->TryPickUp(this);
	}
}

FText ACSTrashActor::GetInteractionText_Implementation() const
{
	return NSLOCTEXT("Interaction", "PickUpTrash", "쓰레기 줍기");
}

void ACSTrashActor::PrepareForPickupPhysics()
{
	if (!IsValid(TrashMesh))
	{
		return;
	}

	if (TrashMesh->IsSimulatingPhysics())
	{
		TrashMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TrashMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	TrashMesh->SetSimulatePhysics(false);

	if (!IsValid(SceneRoot))
	{
		return;
	}

	TrashMesh->AttachToComponent(
		SceneRoot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
}

void ACSTrashActor::EnableWorldDropPhysics()
{
	if (!IsValid(TrashMesh) || TrashMesh->GetStaticMesh() == nullptr)
	{
		return;
	}

	TrashMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TrashMesh->SetSimulatePhysics(true);
}
