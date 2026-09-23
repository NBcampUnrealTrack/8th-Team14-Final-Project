#include "Features/Delivery/CSDeliveryCrateActor.h"

#include "Features/Employees/CSStoreEmployeeCharacter.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Storage/CSFreshnessSafeZoneActor.h"
#include "GameFramework/Pawn.h"
#include "NiagaraComponent.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "TimerManager.h"

namespace
{
void PlayDeliveryCrateGameplaySFX(const AActor* WorldContextActor, ECSAudioEvent AudioEvent)
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

ACSDeliveryCrateActor::ACSDeliveryCrateActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CrateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrateMesh"));
	CrateMesh->SetupAttachment(SceneRoot);
	CrateMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CrateMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	// 상자는 Pawn을 막지 않고 겹침으로만 상호작용합니다.
	// 따라서 이동 가능한 바닥을 NavMesh에서 제거할 이유가 없으며, 이 설정이 없으면
	// 직원 캡슐 반경(75cm)까지 반영된 빈 공간 때문에 상자 중심에서 멀리 멈출 수 있습니다.
	CrateMesh->SetCanEverAffectNavigation(false);
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

	const bool bRuntimeCrateIsUninitialized =
		ProductRow.DataTable == nullptr &&
		ProductRow.RowName.IsNone() &&
		RemainingQuantity <= 0;
	const bool bHasValidPlacedInitialValues =
		IsValid(PlacedProductRow.DataTable) &&
		!PlacedProductRow.RowName.IsNone() &&
		PlacedQuantity > 0;
	if (bRuntimeCrateIsUninitialized && bHasValidPlacedInitialValues)
	{
		// 기존 InitializeCrate를 재사용하며, 발주로 이미 초기화된 상자는 위 조건에서 제외됩니다.
		InitializeCrate(PlacedProductRow, PlacedQuantity);
	}

	EnsureFreshnessRuntimeStateInitialized();
	CacheSpoiledVFXComponent();
	SpoiledVFXStoreGameState = GetWorld() != nullptr
		? GetWorld()->GetGameState<ACSStoreGameState>()
		: nullptr;
	if (IsValid(SpoiledVFXStoreGameState))
	{
		SpoiledVFXStoreGameState->OnBusinessDayStarted.AddUniqueDynamic(
			this,
			&ACSDeliveryCrateActor::HandleSpoiledVFXBusinessDayStarted);
	}

	RefreshSpoiledVFXTimer();
	UpdateSpoiledVFX();
}

void ACSDeliveryCrateActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SpoiledVFXRefreshTimerHandle);

	if (IsValid(SpoiledVFXStoreGameState))
	{
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

bool ACSDeliveryCrateActor::TryReserveForEmployee(ACSStoreEmployeeCharacter* Employee, int32 TaskId)
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

bool ACSDeliveryCrateActor::CanBeReservedByEmployee(
	const ACSStoreEmployeeCharacter* Employee,
	int32 TaskId) const
{
	return IsValid(Employee) &&
		Cast<ACSPlayerCharacter>(GetAttachParentActor()) == nullptr &&
		(!IsValid(ReservedByEmployee) ||
			(ReservedByEmployee == Employee && ReservationTaskId == TaskId));
}

void ACSDeliveryCrateActor::ReleaseEmployeeReservation(
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

bool ACSDeliveryCrateActor::IsReservedByEmployee() const
{
	return IsValid(ReservedByEmployee);
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

	bPackingCrate = false;
	PackedProductStates.Reset();
	ProductRow = InProductRow;
	RemainingQuantity = InQuantity;
	ProductRuntimeState = NewRuntimeState;
	UpdateFreshnessDecayState();
	if (HasActorBegunPlay())
	{
		RefreshSpoiledVFXTimer();
	}
	UpdateSpoiledVFX();
	return true;
}

bool ACSDeliveryCrateActor::CanPackProduct(const ACSProductPickupActor* Product, const AActor* Carrier, int32 TaskId)
{
	if (!IsValid(Product) || Product->IsA<ACSProductPlacementPreviewActor>() ||
		Product->IsExcludedFromStockCount() || Product->GetProductId().IsNone()) return false;
	// 플레이어는 직원 예약보다 우선합니다. 실제 회수 직전에 직원 업무를 취소합니다.
	const bool bPlayerCollector = Cast<ACSPlayerCharacter>(Carrier) != nullptr;
	if (Product->IsReservedByEmployee() && !bPlayerCollector)
	{
		const ACSStoreEmployeeCharacter* Employee = Cast<ACSStoreEmployeeCharacter>(Carrier);
		if (!Employee || !Product->CanBeReservedByEmployee(Employee, TaskId)) return false;
	}
	const AActor* Parent = Product->GetAttachParentActor();
	return !IsValid(Parent) || Parent == Carrier || Cast<ACSShelfActor>(Parent) || Cast<ACSFreshnessSafeZoneActor>(Parent) ||
		(bPlayerCollector && Cast<ACSStoreEmployeeCharacter>(Parent));
}

bool ACSDeliveryCrateActor::InitializePackedProducts(const TArray<ACSProductPickupActor*>& Products)
{
	if (Products.Num() < 2 || Products.Num() > PackingCapacity || !IsValid(Products[0])) return false;
	const FDataTableRowHandle Row = Products[0]->GetProductRow();
	TSet<ACSProductPickupActor*> UniqueProducts;
	for (ACSProductPickupActor* Product : Products)
	{
		if (!IsValid(Product) || Product->IsExcludedFromStockCount() || UniqueProducts.Contains(Product) ||
			Product->GetProductRow().DataTable != Row.DataTable || Product->GetProductRow().RowName != Row.RowName) return false;
		UniqueProducts.Add(Product);
	}
	if (!InitializeCrate(Row, Products.Num())) return false;
	bPackingCrate = true;
	bAwaitingStockroomIntake = false;
	ProductActorClass = Products[0]->GetClass();
	VisualRegistry = Products[0]->GetVisualRegistry();
	for (const ACSProductPickupActor* Product : Products) PackedProductStates.Add(Product->GetProductRuntimeState());
	ProductRuntimeState = PackedProductStates[0];
	UpdateFreshnessDecayState();
	UpdateSpoiledVFX();
	return true;
}

void ACSDeliveryCrateActor::RemovePackedSource(ACSProductPickupActor* Product)
{
	if (!IsValid(Product)) return;
	if (ACSShelfActor* Shelf = Cast<ACSShelfActor>(Product->GetAttachParentActor()))
	{
		Shelf->RemovePlacedItem_Implementation(Product);
	}
	else if (ACSFreshnessSafeZoneActor* Storage = Cast<ACSFreshnessSafeZoneActor>(Product->GetAttachParentActor()))
	{
		Storage->RemoveStoredLooseProduct(Product);
	}
	Product->Destroy();
}

bool ACSDeliveryCrateActor::TryAddPackedProduct(ACSProductPickupActor* Product, AActor* Carrier, int32 TaskId)
{
	if (!bPackingCrate || RemainingQuantity >= PackingCapacity || !CanPackProduct(Product, Carrier, TaskId) ||
		Product->GetProductRow().DataTable != ProductRow.DataTable || Product->GetProductRow().RowName != ProductRow.RowName) return false;
	PackedProductStates.Add(Product->GetProductRuntimeState());
	RemainingQuantity = PackedProductStates.Num();
	UpdateFreshnessDecayState();
	RemovePackedSource(Product);
	OnQuantityChanged.Broadcast(RemainingQuantity);
	UpdateSpoiledVFX();
	return true;
}

void ACSDeliveryCrateActor::SetAwaitingStockroomIntake(bool bInAwaitingStockroomIntake)
{
	// 창고 슬롯에 실제로 적재된 뒤에만 해제하도록 하여, 창고가 가득 찬 경우에는 입고 우선순위가 유지되게 합니다.
	bAwaitingStockroomIntake = bInAwaitingStockroomIntake;
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
	if (bPackingCrate)
	{
		PackedProductStates.RemoveAt(0);
		if (!PackedProductStates.IsEmpty()) ProductRuntimeState = PackedProductStates[0];
		UpdateSpoiledVFX();
	}
	OnQuantityChanged.Broadcast(RemainingQuantity);

	if (RemainingQuantity == 0)
	{
		OnCrateDepleted();
		PlayDeliveryCrateGameplaySFX(this, ECSAudioEvent::DeliveryCrateDisappear);
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
	// 낱개별 상태에도 같은 보관 환경을 적용하며, 기존 배송 상자의 수량 제한은 변경하지 않습니다.
	if (bPackingCrate)
	{
		for (FCSProductRuntimeState& State : PackedProductStates)
		{
			if (!bRequiresFreshnessManagement || bIsFreshnessSafe)
				State.StopFreshnessDecay(World->GetTimeSeconds(), FreshnessSettings->DecayIntervalSeconds, FreshnessSettings->DecayAmountPerInterval);
			else State.StartFreshnessDecay(World->GetTimeSeconds());
		}
		if (!PackedProductStates.IsEmpty()) ProductRuntimeState = PackedProductStates[0];
		return;
	}
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

void ACSDeliveryCrateActor::EnsureFreshnessRuntimeStateInitialized()
{
	if (ProductRuntimeState.bFreshnessInitialized ||
		ProductRow.DataTable == nullptr ||
		ProductRow.RowName.IsNone())
	{
		return;
	}

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::EnsureFreshnessRuntimeStateInitialized"));
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
	ProductRuntimeState.InitializeFreshness(
		FreshnessSettings->InitialFreshness,
		bRequiresFreshnessManagement,
		World->GetTimeSeconds());
	UpdateFreshnessDecayState();
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
	bool bShouldShowSpoiledVFX = bIsExpired || bHasNoFreshness;
	if (bPackingCrate && IsValid(SpoiledVFXStoreGameState))
	{
		const UCSFreshnessSettingsDataAsset* Settings = SpoiledVFXStoreGameState->GetFreshnessSettings();
		for (const FCSProductRuntimeState& State : PackedProductStates)
		{
			bShouldShowSpoiledVFX |= State.IsExpired(SpoiledVFXStoreGameState->CurrentBusinessDay) ||
				(bRequiresFreshnessManagement && Settings && State.GetCurrentFreshness(GetWorld()->GetTimeSeconds(),
					Settings->DecayIntervalSeconds, Settings->DecayAmountPerInterval) <= 0);
		}
	}
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

void ACSDeliveryCrateActor::RefreshSpoiledVFXTimer()
{
	GetWorldTimerManager().ClearTimer(SpoiledVFXRefreshTimerHandle);

	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryCrateActor::RefreshSpoiledVFXTimer"));
	const bool bRequiresFreshnessManagement = ProductData != nullptr &&
		(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
			ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
	if (!bRequiresFreshnessManagement)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(StoreGameState) ? StoreGameState->GetFreshnessSettings() : nullptr;
	const float RefreshIntervalSeconds = IsValid(FreshnessSettings)
		? FMath::Max(0.1f, FreshnessSettings->DecayIntervalSeconds)
		: 1.0f;

	GetWorldTimerManager().SetTimer(
		SpoiledVFXRefreshTimerHandle,
		this,
		&ACSDeliveryCrateActor::HandleSpoiledVFXRefreshTimer,
		RefreshIntervalSeconds,
		true);
}

void ACSDeliveryCrateActor::HandleSpoiledVFXRefreshTimer()
{
	const bool bWasFreshnessInitialized = ProductRuntimeState.bFreshnessInitialized;
	if (!ProductRuntimeState.bFreshnessInitialized)
	{
		EnsureFreshnessRuntimeStateInitialized();
	}
	if (!bWasFreshnessInitialized && ProductRuntimeState.bFreshnessInitialized)
	{
		RefreshSpoiledVFXTimer();
	}

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
