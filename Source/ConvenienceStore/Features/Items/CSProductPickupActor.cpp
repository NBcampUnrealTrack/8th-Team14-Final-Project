#include "Features/Items/CSProductPickupActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/StaticMesh.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "GameFramework/Pawn.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

ACSProductPickupActor::ACSProductPickupActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ProductMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProductMesh"));
	ProductMesh->SetupAttachment(SceneRoot);
	ProductMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ProductMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ProductMesh->SetGenerateOverlapEvents(true);
	ProductMesh->OnComponentBeginOverlap.AddDynamic(
		this,
		&ACSProductPickupActor::HandlePawnOverlap);
}

void ACSProductPickupActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyProductVisual();
}

void ACSProductPickupActor::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(ProductMesh))
	{
		ProductMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		ProductMesh->SetGenerateOverlapEvents(true);
	}

	EnsureProductRuntimeStateInitialized();
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
	const FText ProductDisplayName = GetProductDisplayName();

	return FText::Format(
		NSLOCTEXT("Interaction", "PickUp", "{0} 집기"),
		ProductDisplayName);
}

FText ACSProductPickupActor::GetProductDisplayName() const
{
	if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
	{
		return FText::GetEmpty();
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::GetProductDisplayName"));
	if (ProductData != nullptr && !ProductData->DisplayName.IsEmpty())
	{
		return ProductData->DisplayName;
	}

	return FText::FromName(ProductRow.RowName);
}

bool ACSProductPickupActor::InitializeProduct(FDataTableRowHandle InProductRow)
{
	if (InProductRow.DataTable == nullptr || InProductRow.RowName.IsNone())
	{
		return false;
	}

	const FCSProductDataRow* ProductData =
		InProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::InitializeProduct"));
	if (ProductData == nullptr)
	{
		return false;
	}

	ProductRow = InProductRow;
	ApplyProductVisual();
	if (HasActorBegunPlay())
	{
		EnsureProductRuntimeStateInitialized();
	}
	return true;
}

void ACSProductPickupActor::ApplyProductRuntimeState(
	const FCSProductRuntimeState& InProductRuntimeState)
{
	ProductRuntimeState = InProductRuntimeState;
}

bool ACSProductPickupActor::IsExpired() const
{
	const UWorld* World = GetWorld();
	const ACSStoreGameState* CurrentStoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	return IsValid(CurrentStoreGameState) &&
		ProductRuntimeState.IsExpired(CurrentStoreGameState->CurrentBusinessDay);
}

bool ACSProductPickupActor::IsSellable() const
{
	return !IsExpired();
}

void ACSProductPickupActor::PrepareForPickupPhysics()
{
	if (!IsValid(ProductMesh))
	{
		return;
	}

	if (ProductMesh->IsSimulatingPhysics())
	{
		ProductMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ProductMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	ProductMesh->SetSimulatePhysics(false);

	if (!IsValid(SceneRoot))
	{
		return;
	}

	if (ProductMesh->GetAttachParent() != SceneRoot)
	{
		ProductMesh->AttachToComponent(
			SceneRoot,
			FAttachmentTransformRules::KeepWorldTransform);
	}

	ApplyProductVisual();
}

void ACSProductPickupActor::EnableWorldDropPhysics()
{
	if (!IsValid(ProductMesh) || ProductMesh->GetStaticMesh() == nullptr)
	{
		return;
	}

	ProductMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProductMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ProductMesh->SetGenerateOverlapEvents(true);
	ProductMesh->SetSimulatePhysics(true);
}

void ACSProductPickupActor::SetStorageShelfType(ECSProductShelfType InStorageShelfType)
{
	StorageShelfType = InStorageShelfType;
}

void ACSProductPickupActor::EnsureProductRuntimeStateInitialized()
{
	if (ProductRuntimeState.bExpirationInitialized ||
		ProductRow.DataTable == nullptr ||
		ProductRow.RowName.IsNone())
	{
		return;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::EnsureProductRuntimeStateInitialized"));
	const UWorld* World = GetWorld();
	const ACSStoreGameState* CurrentStoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (ProductData == nullptr || !IsValid(CurrentStoreGameState))
	{
		return;
	}

	ProductRuntimeState.InitializeExpiration(
		CurrentStoreGameState->CurrentBusinessDay,
		ProductData->bPerishable,
		ProductData->ShelfLifeDays);
}

void ACSProductPickupActor::HandlePawnOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32,
	bool,
	const FHitResult&)
{
	if (!IsValid(OtherActor) ||
		OtherActor == this ||
		Cast<APawn>(OtherActor) == nullptr ||
		!IsValid(ProductMesh) ||
		!ProductMesh->IsSimulatingPhysics() ||
		PawnPushImpulseStrength <= 0.0f)
	{
		return;
	}

	FVector PushDirection = OtherActor->GetVelocity();
	PushDirection.Z = 0.0f;
	if (PushDirection.SizeSquared2D() <= FMath::Square(1.0f))
	{
		PushDirection = ProductMesh->GetComponentLocation() - OtherActor->GetActorLocation();
		PushDirection.Z = 0.0f;
		if (!PushDirection.Normalize())
		{
			return;
		}
	}
	else
	{
		PushDirection.Normalize();
	}

	ProductMesh->AddImpulse(
		PushDirection * PawnPushImpulseStrength,
		NAME_None,
		true);
}

void ACSProductPickupActor::ApplyProductVisual()
{
	if (!IsValid(ProductMesh))
	{
		return;
	}

	UStaticMesh* MeshToApply = nullptr;
	FTransform RelativeTransformToApply = FTransform::Identity;

	const FName ProductId = GetProductId();
	if (IsValid(VisualRegistry) && !ProductId.IsNone())
	{
		const FCSProductVisualData* VisualData = VisualRegistry->FindVisualData(ProductId);
		if (VisualData != nullptr && !VisualData->ProductMesh.IsNull())
		{
			if (UStaticMesh* LoadedMesh = VisualData->ProductMesh.LoadSynchronous())
			{
				MeshToApply = LoadedMesh;
				RelativeTransformToApply = VisualData->MeshRelativeTransform;
			}
		}
	}

	if (MeshToApply == nullptr && FallbackMesh != nullptr)
	{
		MeshToApply = FallbackMesh.Get();
		RelativeTransformToApply = FallbackMeshRelativeTransform;
	}

	ProductMesh->SetStaticMesh(MeshToApply);
	ProductMesh->SetRelativeTransform(RelativeTransformToApply);
}
