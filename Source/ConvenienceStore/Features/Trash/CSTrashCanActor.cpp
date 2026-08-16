#include "Features/Trash/CSTrashCanActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Features/Trash/CSTrashable.h"

ACSTrashCanActor::ACSTrashCanActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TrashCanMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrashCanMesh"));
	TrashCanMesh->SetupAttachment(SceneRoot);
	TrashCanMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

FName ACSTrashCanActor::GetFacilityId_Implementation() const
{
	return FacilityRow.RowName;
}

bool ACSTrashCanActor::IsFacilityInstalled_Implementation() const
{
	return bIsInstalled;
}

void ACSTrashCanActor::InitializeAsUninstalledFacility_Implementation()
{
	bIsInstalled = false;
	SetFacilityPhysicsEnabled(true);
}

void ACSTrashCanActor::PrepareForBuildPickup_Implementation()
{
	SetFacilityPhysicsEnabled(false);
}

void ACSTrashCanActor::FinalizeBuildPlacement_Implementation()
{
	SetFacilityPhysicsEnabled(false);
	bIsInstalled = true;
}

bool ACSTrashCanActor::CanDisposeItem(AActor* ItemActor) const
{
	return IsValid(this) &&
		!IsActorBeingDestroyed() &&
		bIsInstalled &&
		GetActorEnableCollision() &&
		IsValid(ItemActor) &&
		ItemActor != this &&
		ItemActor->Implements<UCSTrashable>() &&
		!ItemActor->Implements<UCSBuildableFacility>();
}

bool ACSTrashCanActor::TryDisposeItem(AActor* ItemActor)
{
	if (!CanDisposeItem(ItemActor))
	{
		return false;
	}

	if (TotalDisposedCount < MAX_int32)
	{
		++TotalDisposedCount;
	}

	OnItemDisposed.Broadcast(ItemActor, TotalDisposedCount);
	ItemActor->Destroy();
	return true;
}

void ACSTrashCanActor::SetFacilityPhysicsEnabled(bool bEnabled)
{
	if (!IsValid(TrashCanMesh))
	{
		return;
	}

	if (bEnabled)
	{
		if (!TrashCanMesh->IsSimulatingPhysics() &&
			TrashCanMesh->GetAttachParent() == SceneRoot)
		{
			TrashCanMeshRelativeTransformBeforePhysics =
				TrashCanMesh->GetRelativeTransform();
			bHasCachedTrashCanMeshRelativeTransform = true;
		}

		TrashCanMesh->SetMobility(EComponentMobility::Movable);
		TrashCanMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TrashCanMesh->SetEnableGravity(true);
		TrashCanMesh->SetSimulatePhysics(true);
		if (TrashCanMesh->IsSimulatingPhysics())
		{
			TrashCanMesh->WakeAllRigidBodies();
		}
		return;
	}

	if (TrashCanMesh->IsSimulatingPhysics())
	{
		TrashCanMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TrashCanMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		TrashCanMesh->PutAllRigidBodiesToSleep();
	}
	TrashCanMesh->SetSimulatePhysics(false);
	TrashCanMesh->SetEnableGravity(false);

	bool bAttachedToSceneRoot = TrashCanMesh->GetAttachParent() == SceneRoot;
	if (!bAttachedToSceneRoot && IsValid(SceneRoot))
	{
		bAttachedToSceneRoot = TrashCanMesh->AttachToComponent(
			SceneRoot,
			FAttachmentTransformRules::KeepWorldTransform);
	}

	if (bAttachedToSceneRoot && bHasCachedTrashCanMeshRelativeTransform)
	{
		TrashCanMesh->SetRelativeTransform(
			TrashCanMeshRelativeTransformBeforePhysics,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}
}
