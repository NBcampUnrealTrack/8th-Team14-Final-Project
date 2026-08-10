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

	StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (IsValid(StoreGameState))
	{
		// 다음 영업일이 시작될 때만 변질 상태를 계산해 매 프레임 비용을 만들지 않습니다.
		StoreGameState->OnBusinessDayStarted.AddDynamic(
			this, &ACSProductPickupActor::HandleBusinessDayStarted);
	}
}

void ACSProductPickupActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(StoreGameState))
	{
		StoreGameState->OnBusinessDayStarted.RemoveDynamic(
			this, &ACSProductPickupActor::HandleBusinessDayStarted);
	}

	Super::EndPlay(EndPlayReason);
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

void ACSProductPickupActor::HandleBusinessDayStarted(int32 NewBusinessDay)
{
	UpdateSpoilageForNewBusinessDay(NewBusinessDay);
}

void ACSProductPickupActor::UpdateSpoilageForNewBusinessDay(int32 NewBusinessDay)
{
	if (bIsSpoiled || ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
	{
		return;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::UpdateSpoilageForNewBusinessDay"));
	if (ProductData == nullptr)
	{
		return;
	}

	const bool bRequiresSpecialStorage =
		ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
		ProductData->ShelfType == ECSProductShelfType::FreezerShelf;
	if (!ProductData->bPerishable && !bRequiresSpecialStorage)
	{
		return;
	}

	const bool bProperStorage = IsStorageEnvironmentProper(*ProductData);
	if (ProductData->bPerishable)
	{
		// 일반 유통기한 상품은 정상 보관 시 하루, 부적절한 보관 시 설정한 만큼 빠르게 경과합니다.
		StorageAgeDays += bProperStorage
			? 1
			: FMath::Max(1, ProductData->ImproperStorageAgePerDay);
		if (StorageAgeDays >= FMath::Max(1, ProductData->ShelfLifeDays))
		{
			bIsSpoiled = true;
		}
		return;
	}

	if (!bProperStorage)
	{
		// 유통기한이 없는 냉장·냉동 상품도 일반 매대에 방치하면 별도 기준으로 변질됩니다.
		++StorageAgeDays;
		if (StorageAgeDays >= FMath::Max(1, ProductData->MishandledStorageShelfLifeDays))
		{
			bIsSpoiled = true;
		}
	}
}

bool ACSProductPickupActor::IsStorageEnvironmentProper(
	const FCSProductDataRow& ProductData) const
{
	if (ProductData.ShelfType == ECSProductShelfType::ChilledShelf)
	{
		return StorageShelfType == ECSProductShelfType::ChilledShelf;
	}

	if (ProductData.ShelfType == ECSProductShelfType::FreezerShelf)
	{
		return StorageShelfType == ECSProductShelfType::FreezerShelf;
	}

	return true;
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
