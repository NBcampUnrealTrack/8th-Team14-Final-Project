#include "Features/Items/CSProductPickupActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/StaticMesh.h"
#include "Features/Player/CSPlayerCharacter.h"

ACSProductPickupActor::ACSProductPickupActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ProductMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProductMesh"));
	ProductMesh->SetupAttachment(SceneRoot);
	ProductMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSProductPickupActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyProductVisual();
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
	return true;
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
