#include "Features/Delivery/CSDeliveryCrateActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/World.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "GameFramework/Pawn.h"
#include "NiagaraComponent.h"
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

	CacheSpoiledVFXComponent();
	SpoiledVFXStoreGameState = GetWorld() != nullptr
		? GetWorld()->GetGameState<ACSStoreGameState>()
		: nullptr;
	if (IsValid(SpoiledVFXStoreGameState))
	{
		SpoiledVFXStoreGameState->OnGameTimeChanged.AddUniqueDynamic(
			this,
			&ACSDeliveryCrateActor::HandleSpoiledVFXGameTimeChanged);
		SpoiledVFXStoreGameState->OnBusinessDayStarted.AddUniqueDynamic(
			this,
			&ACSDeliveryCrateActor::HandleSpoiledVFXBusinessDayStarted);
	}

	UpdateSpoiledVFX();
}

void ACSDeliveryCrateActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(SpoiledVFXStoreGameState))
	{
		SpoiledVFXStoreGameState->OnGameTimeChanged.RemoveDynamic(
			this,
			&ACSDeliveryCrateActor::HandleSpoiledVFXGameTimeChanged);
		SpoiledVFXStoreGameState->OnBusinessDayStarted.RemoveDynamic(
			this,
			&ACSDeliveryCrateActor::HandleSpoiledVFXBusinessDayStarted);
	}

	SpoiledVFXStoreGameState = nullptr;
	SpoiledVFXComponent = nullptr;
	Super::EndPlay(EndPlayReason);
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
	if (const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		StoreGameState->GetFreshnessSettings();
		IsValid(FreshnessSettings))
	{
		const bool bRequiresFreshnessManagement =
			ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
			ProductData->ShelfType == ECSProductShelfType::FreezerShelf;
		NewRuntimeState.InitializeFreshness(
			FreshnessSettings->InitialFreshness,
			bRequiresFreshnessManagement,
			World->GetTimeSeconds());
	}

	ProductRow = InProductRow;
	RemainingQuantity = InQuantity;
	ProductRuntimeState = NewRuntimeState;
	UpdateFreshnessDecayState();
	UpdateSpoiledVFX();
	return true;
}

int32 ACSDeliveryCrateActor::GetCurrentFreshness() const
{
	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(StoreGameState) ? StoreGameState->GetFreshnessSettings() : nullptr;
	if (!IsValid(World) || !IsValid(FreshnessSettings))
	{
		return FMath::Clamp(ProductRuntimeState.Freshness, 0, 100);
	}

	return ProductRuntimeState.GetCurrentFreshness(
		World->GetTimeSeconds(),
		FreshnessSettings->DecayIntervalSeconds,
		FreshnessSettings->DecayAmountPerInterval);
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

void ACSDeliveryCrateActor::EnterFreshnessSafeZone(
	ECSProductShelfType SafeStorageType)
{
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::EnterFreshnessSafeZone"));
	if (ProductData == nullptr || ProductData->ShelfType != SafeStorageType ||
		(SafeStorageType != ECSProductShelfType::ChilledShelf &&
			SafeStorageType != ECSProductShelfType::FreezerShelf))
	{
		return;
	}

	if (MatchingFreshnessSafeZoneCount < MAX_int32)
	{
		++MatchingFreshnessSafeZoneCount;
	}
	UpdateFreshnessDecayState();
}

void ACSDeliveryCrateActor::ExitFreshnessSafeZone(
	ECSProductShelfType SafeStorageType)
{
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::ExitFreshnessSafeZone"));
	if (ProductData == nullptr || ProductData->ShelfType != SafeStorageType ||
		MatchingFreshnessSafeZoneCount <= 0)
	{
		return;
	}

	--MatchingFreshnessSafeZoneCount;
	UpdateFreshnessDecayState();
}

void ACSDeliveryCrateActor::SetHeldByPlayerForFreshness(bool bIsHeldByPlayer)
{
	if (bIsHeldByPlayerForFreshness == bIsHeldByPlayer)
	{
		return;
	}

	bIsHeldByPlayerForFreshness = bIsHeldByPlayer;
	UpdateFreshnessDecayState();
	if (!bIsHeldByPlayerForFreshness && IsValid(CrateMesh))
	{
		CrateMesh->UpdateOverlaps();
	}
}

void ACSDeliveryCrateActor::UpdateFreshnessDecayState()
{
	if (!ProductRuntimeState.bFreshnessInitialized)
	{
		return;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::UpdateFreshnessDecayState"));
	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(StoreGameState) ? StoreGameState->GetFreshnessSettings() : nullptr;
	if (ProductData == nullptr || !IsValid(World) || !IsValid(FreshnessSettings))
	{
		return;
	}

	const bool bRequiresFreshnessManagement =
		ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
		ProductData->ShelfType == ECSProductShelfType::FreezerShelf;
	const bool bIsFreshnessSafe = !bIsHeldByPlayerForFreshness &&
		MatchingFreshnessSafeZoneCount > 0;
	if (!bRequiresFreshnessManagement || bIsFreshnessSafe)
	{
		ProductRuntimeState.StopFreshnessDecay(
			World->GetTimeSeconds(),
			FreshnessSettings->DecayIntervalSeconds,
			FreshnessSettings->DecayAmountPerInterval);
		return;
	}

	ProductRuntimeState.StartFreshnessDecay(World->GetTimeSeconds());
}

void ACSDeliveryCrateActor::CacheSpoiledVFXComponent()
{
	SpoiledVFXComponent = nullptr;

	TArray<UNiagaraComponent*> NiagaraComponents;
	GetComponents<UNiagaraComponent>(NiagaraComponents);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		if (IsValid(NiagaraComponent) &&
			NiagaraComponent->GetFName() == TEXT("SpoiledVFX"))
		{
			SpoiledVFXComponent = NiagaraComponent;
			return;
		}
	}
}

void ACSDeliveryCrateActor::UpdateSpoiledVFX()
{
	if (!IsValid(SpoiledVFXComponent))
	{
		return;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::UpdateSpoiledVFX"));
	const bool bRequiresFreshnessManagement = ProductData != nullptr &&
		(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
			ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
	const bool bHasNoFreshness = bRequiresFreshnessManagement &&
		ProductRuntimeState.bFreshnessInitialized &&
		GetCurrentFreshness() <= 0;
	const bool bIsExpired = IsValid(SpoiledVFXStoreGameState) &&
		ProductRuntimeState.IsExpired(SpoiledVFXStoreGameState->CurrentBusinessDay);
	const bool bShouldShowSpoiledVFX = bIsExpired || bHasNoFreshness;
	if (SpoiledVFXComponent->IsActive() == bShouldShowSpoiledVFX)
	{
		return;
	}

	if (bShouldShowSpoiledVFX)
	{
		SpoiledVFXComponent->Activate();
	}
	else
	{
		SpoiledVFXComponent->Deactivate();
	}
}

void ACSDeliveryCrateActor::HandleSpoiledVFXGameTimeChanged(float NewGameMinutes)
{
	(void)NewGameMinutes;
	UpdateSpoiledVFX();
}

void ACSDeliveryCrateActor::HandleSpoiledVFXBusinessDayStarted(int32 NewBusinessDay)
{
	(void)NewBusinessDay;
	UpdateSpoiledVFX();
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
