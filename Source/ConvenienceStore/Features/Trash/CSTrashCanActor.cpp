#include "Features/Trash/CSTrashCanActor.h"

#include "Components/SceneComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Features/Facilities/CSSaveIdComponent.h"
#include "Features/Trash/CSTrashable.h"
#include "Systems/Audio/CSAudioSubsystem.h"

namespace
{
void PlayTrashCanGameplaySFX(const AActor* WorldContextActor, ECSAudioEvent AudioEvent)
{
	if (!IsValid(WorldContextActor))
	{
		return;
	}

	if (UGameInstance* GameInstance = WorldContextActor->GetGameInstance();
		IsValid(GameInstance))
	{
		if (UCSAudioSubsystem* AudioSubsystem =
			GameInstance->GetSubsystem<UCSAudioSubsystem>(); IsValid(AudioSubsystem))
		{
			AudioSubsystem->PlaySFX(AudioEvent);
		}
	}
}
}

ACSTrashCanActor::ACSTrashCanActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TrashCanMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrashCanMesh"));
	TrashCanMesh->SetupAttachment(SceneRoot);
	TrashCanMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	SaveIdComponent = CreateDefaultSubobject<UCSSaveIdComponent>(TEXT("SaveIdComponent"));
	FairyApproachPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("FairyApproachPoint"));
	FairyApproachPoint->SetupAttachment(SceneRoot);
	FairyApproachPoint->SetRelativeLocation(FVector(100, 0, 0));
	// 기존 앞면 지점은 유지하고 나머지 세 면의 지점을 추가합니다. 각 위치를 블루프린트에서 조절해 주세요.
	FairyApproachBack = CreateDefaultSubobject<UArrowComponent>(TEXT("FairyApproachBack"));
	FairyApproachBack->SetupAttachment(SceneRoot);
	FairyApproachBack->SetRelativeLocation(FVector(-200, 0, 0));
	FairyApproachLeft = CreateDefaultSubobject<UArrowComponent>(TEXT("FairyApproachLeft"));
	FairyApproachLeft->SetupAttachment(SceneRoot);
	FairyApproachLeft->SetRelativeLocation(FVector(0, -200, 0));
	FairyApproachRight = CreateDefaultSubobject<UArrowComponent>(TEXT("FairyApproachRight"));
	FairyApproachRight->SetupAttachment(SceneRoot);
	FairyApproachRight->SetRelativeLocation(FVector(0, 200, 0));
	FairyDropPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("FairyDropPoint"));
	FairyDropPoint->SetupAttachment(SceneRoot);
	FairyDropPoint->SetRelativeLocation(FVector(0, 0, 150));
}

bool ACSTrashCanActor::IsAvailableForFairy() const
{
	return bIsInstalled && GetActorEnableCollision() && !GetAttachParentActor() && !IsActorBeingDestroyed();
}

TArray<UArrowComponent*> ACSTrashCanActor::GetFairyApproachPoints() const
{
	return {FairyApproachPoint.Get(), FairyApproachBack.Get(), FairyApproachLeft.Get(), FairyApproachRight.Get()};
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
	PlayTrashCanGameplaySFX(this, ECSAudioEvent::TrashDispose);
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
