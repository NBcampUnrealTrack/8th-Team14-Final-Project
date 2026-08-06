#include "Features/Delivery/CSDeliveryCrateActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"

ACSDeliveryCrateActor::ACSDeliveryCrateActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CrateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrateMesh"));
	CrateMesh->SetupAttachment(SceneRoot);
	CrateMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

bool ACSDeliveryCrateActor::InitializeCrate(
	FDataTableRowHandle InProductRow,
	int32 InQuantity)
{
	if (InProductRow.DataTable == nullptr ||
		InProductRow.RowName.IsNone() ||
		InQuantity < 1)
	{
		return false;
	}

	const FCSProductDataRow* ProductData =
		InProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::InitializeCrate"));
	if (ProductData == nullptr)
	{
		return false;
	}

	ProductRow = InProductRow;
	RemainingQuantity = InQuantity;
	return true;
}

FName ACSDeliveryCrateActor::GetProductId() const
{
	if (ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
	{
		return NAME_None;
	}

	return ProductRow.RowName;
}

FText ACSDeliveryCrateActor::GetProductDisplayName() const
{
	const FName ProductId = GetProductId();
	if (ProductId.IsNone())
	{
		return FText::GetEmpty();
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::GetProductDisplayName"));
	if (ProductData != nullptr && !ProductData->DisplayName.IsEmpty())
	{
		return ProductData->DisplayName;
	}

	return FText::FromName(ProductId);
}

int32 ACSDeliveryCrateActor::GetRemainingQuantity() const
{
	return RemainingQuantity;
}

TSoftObjectPtr<UTexture2D> ACSDeliveryCrateActor::GetProductIcon() const
{
	const FName ProductId = GetProductId();
	if (!IsValid(VisualRegistry) || ProductId.IsNone())
	{
		return TSoftObjectPtr<UTexture2D>();
	}

	const FCSProductVisualData* VisualData = VisualRegistry->FindVisualData(ProductId);
	return VisualData != nullptr
		? VisualData->ProductIcon
		: TSoftObjectPtr<UTexture2D>();
}

bool ACSDeliveryCrateActor::ConsumeOne()
{
	if (RemainingQuantity < 1)
	{
		return false;
	}

	--RemainingQuantity;
	OnQuantityChanged.Broadcast(RemainingQuantity);

	if (RemainingQuantity == 0)
	{
		OnCrateDepleted();
		Destroy();
	}

	return true;
}
