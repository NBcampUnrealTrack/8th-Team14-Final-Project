#include "Features/Delivery/CSDeliveryCrateActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/World.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "GameFramework/Pawn.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

ACSDeliveryCrateActor::ACSDeliveryCrateActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CrateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrateMesh"));
	CrateMesh->SetupAttachment(SceneRoot);
	CrateMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CrateMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CrateMesh->SetGenerateOverlapEvents(true);
	CrateMesh->OnComponentBeginOverlap.AddDynamic(
		this,
		&ACSDeliveryCrateActor::HandlePawnOverlap);
}

void ACSDeliveryCrateActor::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(CrateMesh))
	{
		CrateMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		CrateMesh->SetGenerateOverlapEvents(true);
	}
}

void ACSDeliveryCrateActor::Interact_Implementation(AActor* Interactor)
{
	if (ACSPlayerCharacter* PlayerCharacter = Cast<ACSPlayerCharacter>(Interactor))
	{
		PlayerCharacter->TryPickUp(this);
	}
}

FText ACSDeliveryCrateActor::GetInteractionText_Implementation() const
{
	return FText::Format(
		NSLOCTEXT("Interaction", "PickUpDeliveryCrate", "{0} 배송 상자 집기"),
		GetProductDisplayName());
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

	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	if (!IsValid(StoreGameState))
	{
		return false;
	}

	FCSProductRuntimeState NewRuntimeState;
	NewRuntimeState.InitializeExpiration(
		StoreGameState->CurrentBusinessDay,
		ProductData->bPerishable,
		ProductData->ShelfLifeDays);

	ProductRow = InProductRow;
	RemainingQuantity = InQuantity;
	ProductRuntimeState = NewRuntimeState;
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

FDataTableRowHandle ACSDeliveryCrateActor::GetProductRow() const
{
	return ProductRow;
}

TSubclassOf<ACSProductPickupActor> ACSDeliveryCrateActor::GetProductActorClass() const
{
	return ProductActorClass;
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

ACSProductPickupActor* ACSDeliveryCrateActor::SpawnProductActor() const
{
	if (ProductActorClass == nullptr ||
		RemainingQuantity < 1 ||
		ProductRow.DataTable == nullptr ||
		ProductRow.RowName.IsNone() ||
		GetWorld() == nullptr)
	{
		return nullptr;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::SpawnProductActor"));
	if (ProductData == nullptr)
	{
		return nullptr;
	}

	const FTransform SpawnTransform = GetActorTransform();
	ACSProductPickupActor* SpawnedProduct =
		GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
			ProductActorClass,
			SpawnTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(SpawnedProduct) ||
		!SpawnedProduct->InitializeProduct(ProductRow))
	{
		if (IsValid(SpawnedProduct))
		{
			SpawnedProduct->Destroy();
		}

		return nullptr;
	}

	SpawnedProduct->ApplyProductRuntimeState(ProductRuntimeState);
	SpawnedProduct->FinishSpawning(SpawnTransform);
	if (!IsValid(SpawnedProduct))
	{
		return nullptr;
	}

	return SpawnedProduct;
}

void ACSDeliveryCrateActor::PrepareForPickupPhysics()
{
	if (!IsValid(CrateMesh))
	{
		return;
	}

	FTransform DefaultRelativeTransform = CrateMesh->GetRelativeTransform();
	const ACSDeliveryCrateActor* ClassDefaults =
		GetClass()->GetDefaultObject<ACSDeliveryCrateActor>();
	if (IsValid(ClassDefaults) &&
		ClassDefaults != this &&
		IsValid(ClassDefaults->CrateMesh))
	{
		DefaultRelativeTransform = ClassDefaults->CrateMesh->GetRelativeTransform();
	}

	if (CrateMesh->IsSimulatingPhysics())
	{
		CrateMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		CrateMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	CrateMesh->SetSimulatePhysics(false);

	if (!IsValid(SceneRoot))
	{
		return;
	}

	if (CrateMesh->GetAttachParent() != SceneRoot)
	{
		CrateMesh->AttachToComponent(
			SceneRoot,
			FAttachmentTransformRules::KeepWorldTransform);
	}
	CrateMesh->SetRelativeTransform(DefaultRelativeTransform);
}

void ACSDeliveryCrateActor::EnableWorldDropPhysics()
{
	if (!IsValid(CrateMesh) || CrateMesh->GetStaticMesh() == nullptr)
	{
		return;
	}

	CrateMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CrateMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CrateMesh->SetGenerateOverlapEvents(true);
	CrateMesh->SetSimulatePhysics(true);
}

void ACSDeliveryCrateActor::HandlePawnOverlap(
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
		!IsValid(CrateMesh) ||
		!CrateMesh->IsSimulatingPhysics() ||
		PawnPushImpulseStrength <= 0.0f)
	{
		return;
	}

	FVector PushDirection = OtherActor->GetVelocity();
	PushDirection.Z = 0.0f;
	if (PushDirection.SizeSquared2D() <= FMath::Square(1.0f))
	{
		PushDirection = CrateMesh->GetComponentLocation() - OtherActor->GetActorLocation();
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

	CrateMesh->AddImpulse(
		PushDirection * PawnPushImpulseStrength,
		NAME_None,
		true);
}
