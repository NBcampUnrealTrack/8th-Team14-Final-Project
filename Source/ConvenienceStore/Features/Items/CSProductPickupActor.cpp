#include "Features/Items/CSProductPickupActor.h"

#include "Features/Employees/CSStoreEmployeeCharacter.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "GameFramework/Pawn.h"
#include "NiagaraComponent.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "TimerManager.h"

ACSProductPickupActor::ACSProductPickupActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ProductMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProductMesh"));
	ProductMesh->SetupAttachment(SceneRoot);
	ProductMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ProductMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// [백무송]
	// 바닥에 떨어진 상품이 NavMesh를 변경해 손님 이동 경로를 막지 않도록 설정
	// 콜리전 응답 재설정과 별개로 내비게이션 영향 차단을 유지
	ProductMesh->SetCanEverAffectNavigation(false);

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
	CacheSpoiledVFXComponent();

	SpoiledVFXStoreGameState = GetWorld() != nullptr
		? GetWorld()->GetGameState<ACSStoreGameState>()
		: nullptr;
	if (IsValid(SpoiledVFXStoreGameState))
	{
		SpoiledVFXStoreGameState->OnBusinessDayStarted.AddUniqueDynamic(
			this,
			&ACSProductPickupActor::HandleSpoiledVFXBusinessDayStarted);
	}

	RefreshSpoiledVFXTimer();
	UpdateSpoiledVFX();
}

void ACSProductPickupActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SpoiledVFXRefreshTimerHandle);

	if (IsValid(SpoiledVFXStoreGameState))
	{
		SpoiledVFXStoreGameState->OnBusinessDayStarted.RemoveDynamic(
			this,
			&ACSProductPickupActor::HandleSpoiledVFXBusinessDayStarted);
	}

	SpoiledVFXStoreGameState = nullptr;
	SpoiledVFXComponent = nullptr;
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

bool ACSProductPickupActor::TryReserveForEmployee(ACSStoreEmployeeCharacter* Employee, int32 TaskId)
{
	if (!CanBeReservedByEmployee(Employee, TaskId))
	{
		return false;
	}

	ReservedByEmployee = Employee;
	ReservationTaskId = TaskId;
	ReservationStartTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f;
	return true;
}

bool ACSProductPickupActor::CanBeReservedByEmployee(
	const ACSStoreEmployeeCharacter* Employee,
	int32 TaskId) const
{
	return IsValid(Employee) &&
		(!IsValid(ReservedByEmployee) ||
			(ReservedByEmployee == Employee && ReservationTaskId == TaskId));
}

void ACSProductPickupActor::ReleaseEmployeeReservation(
	const ACSStoreEmployeeCharacter* Employee,
	int32 TaskId)
{
	if (ReservedByEmployee == Employee && ReservationTaskId == TaskId)
	{
		ReservedByEmployee = nullptr;
		ReservationTaskId = INDEX_NONE;
		ReservationStartTimeSeconds = 0.0f;
	}
}

bool ACSProductPickupActor::IsReservedByEmployee() const
{
	return IsValid(ReservedByEmployee);
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
		RefreshSpoiledVFXTimer();
		UpdateSpoiledVFX();
	}
	return true;
}

void ACSProductPickupActor::ApplyProductRuntimeState(
	const FCSProductRuntimeState& InProductRuntimeState)
{
	ProductRuntimeState = InProductRuntimeState;
	UpdateFreshnessDecayState();
	if (HasActorBegunPlay())
	{
		RefreshSpoiledVFXTimer();
	}
	UpdateSpoiledVFX();
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
	if (IsExpired())
	{
		return false;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::IsSellable"));
	const bool bRequiresFreshnessManagement = ProductData != nullptr &&
		(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
			ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
	if (!bRequiresFreshnessManagement || !ProductRuntimeState.bFreshnessInitialized)
	{
		return true;
	}

	return GetCurrentFreshness() > 0;
}

int32 ACSProductPickupActor::GetCurrentFreshness() const
{
	const UWorld* World = GetWorld();
	const ACSStoreGameState* CurrentStoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(CurrentStoreGameState)
			? CurrentStoreGameState->GetFreshnessSettings()
			: nullptr;
	if (!IsValid(World) || !IsValid(FreshnessSettings))
	{
		return FMath::Clamp(ProductRuntimeState.Freshness, 0, 100);
	}

	return ProductRuntimeState.GetCurrentFreshness(
		World->GetTimeSeconds(),
		FreshnessSettings->DecayIntervalSeconds,
		FreshnessSettings->DecayAmountPerInterval);
}

void ACSProductPickupActor::PrepareForPickupPhysics()
{
	if (!IsValid(ProductMesh))
	{
		return;
	}

	// 창고에서 꺼낸 상품은 다시 일반 상품의 충돌 설정으로 되돌린 뒤 손·진열대·바닥으로 이동합니다.
	SetStoredForPlayerInteraction(false);

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

	SetStoredForPlayerInteraction(false);
	ProductMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProductMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ProductMesh->SetGenerateOverlapEvents(true);
	ProductMesh->SetSimulatePhysics(true);
}

void ACSProductPickupActor::SetStoredForPlayerInteraction(bool bStoredForPlayerInteraction)
{
	if (!IsValid(ProductMesh))
	{
		return;
	}

	SetActorEnableCollision(true);
	ProductMesh->SetSimulatePhysics(false);
	ProductMesh->SetGenerateOverlapEvents(!bStoredForPlayerInteraction);
	ProductMesh->SetCollisionResponseToAllChannels(
		bStoredForPlayerInteraction ? ECR_Ignore : ECR_Block);
	ProductMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ProductMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ProductMesh->SetCollisionEnabled(
		bStoredForPlayerInteraction ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
}

void ACSProductPickupActor::SetStorageShelfType(ECSProductShelfType InStorageShelfType)
{
	StorageShelfType = InStorageShelfType;
	EnsureProductRuntimeStateInitialized();
	UpdateFreshnessDecayState();
}

void ACSProductPickupActor::EnterFreshnessSafeZone(
	ECSProductShelfType SafeStorageType)
{
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::EnterFreshnessSafeZone"));
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

void ACSProductPickupActor::ExitFreshnessSafeZone(
	ECSProductShelfType SafeStorageType)
{
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::ExitFreshnessSafeZone"));
	if (ProductData == nullptr || ProductData->ShelfType != SafeStorageType ||
		MatchingFreshnessSafeZoneCount <= 0)
	{
		return;
	}

	--MatchingFreshnessSafeZoneCount;
	UpdateFreshnessDecayState();
}

void ACSProductPickupActor::SetHeldByPlayerForFreshness(bool bIsHeldByPlayer)
{
	if (bIsHeldByPlayerForFreshness == bIsHeldByPlayer)
	{
		return;
	}

	bIsHeldByPlayerForFreshness = bIsHeldByPlayer;
	UpdateFreshnessDecayState();
	if (!bIsHeldByPlayerForFreshness && IsValid(ProductMesh))
	{
		ProductMesh->UpdateOverlaps();
	}
}

void ACSProductPickupActor::SetFreshnessFrozenForDisplay(bool bFrozen)
{
	if (bIsFreshnessFrozenForDisplay == bFrozen)
	{
		return;
	}

	bIsFreshnessFrozenForDisplay = bFrozen;
	UpdateFreshnessDecayState();
}

void ACSProductPickupActor::EnsureProductRuntimeStateInitialized()
{
	if (ProductRow.DataTable == nullptr ||
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

	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		CurrentStoreGameState->GetFreshnessSettings();
	if (!IsValid(FreshnessSettings))
	{
		return;
	}

	const bool bRequiresFreshnessManagement =
		ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
		ProductData->ShelfType == ECSProductShelfType::FreezerShelf;
	const bool bIsSuitableStorage =
		bRequiresFreshnessManagement && StorageShelfType == ProductData->ShelfType;
	const bool bIsInMatchingSafeZone = MatchingFreshnessSafeZoneCount > 0;
	ProductRuntimeState.InitializeFreshness(
		FreshnessSettings->InitialFreshness,
		bRequiresFreshnessManagement &&
			(bIsHeldByPlayerForFreshness ||
				(!bIsSuitableStorage && !bIsInMatchingSafeZone)),
		World->GetTimeSeconds());
	UpdateFreshnessDecayState();
}

void ACSProductPickupActor::UpdateFreshnessDecayState()
{
	if (!ProductRuntimeState.bFreshnessInitialized)
	{
		return;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::UpdateFreshnessDecayState"));
	const UWorld* World = GetWorld();
	const ACSStoreGameState* CurrentStoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(CurrentStoreGameState)
			? CurrentStoreGameState->GetFreshnessSettings()
			: nullptr;
	if (ProductData == nullptr || !IsValid(World) || !IsValid(FreshnessSettings))
	{
		return;
	}
	if (bIsFreshnessFrozenForDisplay)
	{
		ProductRuntimeState.StopFreshnessDecay(
			World->GetTimeSeconds(),
			FreshnessSettings->DecayIntervalSeconds,
			FreshnessSettings->DecayAmountPerInterval);
		return;
	}

	const bool bRequiresFreshnessManagement =
		ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
		ProductData->ShelfType == ECSProductShelfType::FreezerShelf;
	const bool bIsSuitableStorage =
		bRequiresFreshnessManagement && StorageShelfType == ProductData->ShelfType;
	const bool bIsInMatchingSafeZone = MatchingFreshnessSafeZoneCount > 0;
	const bool bIsFreshnessSafe = !bIsHeldByPlayerForFreshness &&
		(bIsSuitableStorage || bIsInMatchingSafeZone);
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

void ACSProductPickupActor::CacheSpoiledVFXComponent()
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

void ACSProductPickupActor::UpdateSpoiledVFX()
{
	if (!IsValid(SpoiledVFXComponent))
	{
		return;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::UpdateSpoiledVFX"));
	const bool bRequiresFreshnessManagement = ProductData != nullptr &&
		(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
			ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
	const bool bHasNoFreshness = bRequiresFreshnessManagement &&
		ProductRuntimeState.bFreshnessInitialized &&
		GetCurrentFreshness() <= 0;
	const bool bShouldShowSpoiledVFX = IsExpired() || bHasNoFreshness;
	if (bShouldShowSpoiledVFX)
	{
		if (!SpoiledVFXComponent->IsActive() || SpoiledVFXComponent->IsComplete())
		{
			SpoiledVFXComponent->Activate(true);
		}

		return;
	}

	if (SpoiledVFXComponent->IsActive() && !SpoiledVFXComponent->IsComplete())
	{
		SpoiledVFXComponent->Deactivate();
	}
}

void ACSProductPickupActor::RefreshSpoiledVFXTimer()
{
	GetWorldTimerManager().ClearTimer(SpoiledVFXRefreshTimerHandle);

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSProductPickupActor::RefreshSpoiledVFXTimer"));
	const bool bRequiresFreshnessManagement = ProductData != nullptr &&
		(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
			ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
	if (!bRequiresFreshnessManagement)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const ACSStoreGameState* CurrentStoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(CurrentStoreGameState)
			? CurrentStoreGameState->GetFreshnessSettings()
			: nullptr;
	const float RefreshIntervalSeconds = IsValid(FreshnessSettings)
		? FMath::Max(0.1f, FreshnessSettings->DecayIntervalSeconds)
		: 1.0f;

	GetWorldTimerManager().SetTimer(
		SpoiledVFXRefreshTimerHandle,
		this,
		&ACSProductPickupActor::HandleSpoiledVFXRefreshTimer,
		RefreshIntervalSeconds,
		true);
}

void ACSProductPickupActor::HandleSpoiledVFXRefreshTimer()
{
	const bool bWasFreshnessInitialized = ProductRuntimeState.bFreshnessInitialized;
	if (!ProductRuntimeState.bFreshnessInitialized)
	{
		EnsureProductRuntimeStateInitialized();
	}
	if (!bWasFreshnessInitialized && ProductRuntimeState.bFreshnessInitialized)
	{
		RefreshSpoiledVFXTimer();
	}

	UpdateSpoiledVFX();
}

void ACSProductPickupActor::HandleSpoiledVFXBusinessDayStarted(int32 NewBusinessDay)
{
	(void)NewBusinessDay;
	UpdateSpoiledVFX();
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
