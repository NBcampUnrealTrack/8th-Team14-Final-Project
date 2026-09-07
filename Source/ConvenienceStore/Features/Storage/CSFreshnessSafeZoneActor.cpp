#include "Features/Storage/CSFreshnessSafeZoneActor.h"

#include "Components/BoxComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "NavigationSystem.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 모든 창고 BP가 공유하는 미리 생성된 최대 상자 슬롯 수입니다.
	constexpr int32 MaxCrateStorageSlots = 9;
}

ACSFreshnessSafeZoneActor::ACSFreshnessSafeZoneActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SafeZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SafeZoneBox"));
	SafeZoneBox->SetupAttachment(SceneRoot);
	SafeZoneBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SafeZoneBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	// 보관 구역은 안의 낱개 상품 조준을 가로막지 않도록 Visibility Trace를 무시합니다.
	SafeZoneBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	SafeZoneBox->SetGenerateOverlapEvents(true);
	SafeZoneBox->OnComponentBeginOverlap.AddDynamic(
		this,
		&ACSFreshnessSafeZoneActor::HandleSafeZoneBeginOverlap);
	SafeZoneBox->OnComponentEndOverlap.AddDynamic(
		this,
		&ACSFreshnessSafeZoneActor::HandleSafeZoneEndOverlap);

	StorageApproachPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("StorageApproachPoint"));
	StorageApproachPoint->SetupAttachment(SceneRoot);
	StorageApproachPoint->SetRelativeLocation(FVector(180.0f, 0.0f, 0.0f));
	StorageApproachPoint->SetHiddenInGame(true);

	// 창고 BP에서 화살표만 옮겨 상자 위치를 직관적으로 정할 수 있도록 기본 슬롯을 준비합니다.
	for (int32 SlotIndex = 0; SlotIndex < MaxCrateStorageSlots; ++SlotIndex)
	{
		UArrowComponent* CrateSlotPoint = CreateDefaultSubobject<UArrowComponent>(
			FName(*FString::Printf(TEXT("CrateStorageSlot_%02d"), SlotIndex + 1)));
		CrateSlotPoint->SetupAttachment(SceneRoot);
		CrateSlotPoint->SetRelativeLocation(FVector(0.0f, SlotIndex * 110.0f, 0.0f));
		CrateSlotPoint->SetHiddenInGame(true);
		CrateStorageSlotPoints.Add(CrateSlotPoint);

		UBoxComponent* SlotInteractionBox = CreateDefaultSubobject<UBoxComponent>(
			FName(*FString::Printf(TEXT("CrateStorageSlotInteract_%02d"), SlotIndex + 1)));
		SlotInteractionBox->SetupAttachment(CrateSlotPoint);
		SlotInteractionBox->SetBoxExtent(FVector(55.0f, 55.0f, 45.0f));
		SlotInteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SlotInteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
		SlotInteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		SlotInteractionBox->SetGenerateOverlapEvents(false);
		SlotInteractionBox->SetHiddenInGame(true);
		CrateStorageSlotInteractionBoxes.Add(SlotInteractionBox);
	}

	LooseProductStorageZone = CreateDefaultSubobject<UBoxComponent>(TEXT("LooseProductStorageZone"));
	LooseProductStorageZone->SetupAttachment(SceneRoot);
	LooseProductStorageZone->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	LooseProductStorageZone->SetBoxExtent(FVector(180.0f, 120.0f, 80.0f));
	LooseProductStorageZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LooseProductDepositPoint = CreateDefaultSubobject<UBoxComponent>(TEXT("LooseProductDepositPoint"));
	LooseProductDepositPoint->SetupAttachment(SceneRoot);
	// 실제 상품 격자와 겹치지 않도록 기본값은 보관소 앞쪽에 두며, BP에서 자유롭게 옮기실 수 있습니다.
	LooseProductDepositPoint->SetRelativeLocation(FVector(0.0f, -170.0f, 80.0f));
	LooseProductDepositPoint->SetBoxExtent(FVector(70.0f, 25.0f, 70.0f));
	LooseProductDepositPoint->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LooseProductDepositPoint->SetCollisionResponseToAllChannels(ECR_Ignore);
	LooseProductDepositPoint->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	LooseProductDepositPoint->SetGenerateOverlapEvents(false);
	LooseProductDepositPoint->SetHiddenInGame(true);

	// 수레 적재 프리뷰와 동일한 머티리얼을 기본으로 사용하여 보관소에서도 일관된 안내 색을 제공합니다.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ValidPreviewMaterialFinder(
		TEXT("/Game/ConvenienceStore/Asset/ProductAsset/Material/MI_CSPlacementPreview_Valid.MI_CSPlacementPreview_Valid"));
	if (ValidPreviewMaterialFinder.Succeeded())
	{
		ValidCrateStoragePreviewMaterial = ValidPreviewMaterialFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> InvalidPreviewMaterialFinder(
		TEXT("/Game/ConvenienceStore/Asset/ProductAsset/Material/MI_CSPlacementPreview_Invalid.MI_CSPlacementPreview_Invalid"));
	if (InvalidPreviewMaterialFinder.Succeeded())
	{
		InvalidCrateStoragePreviewMaterial = InvalidPreviewMaterialFinder.Object;
	}

	CrateStoragePreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CrateStoragePreviewMesh"));
	CrateStoragePreviewMesh->SetupAttachment(SceneRoot);
	// 고스트는 시각 안내 전용이므로 물리·상호작용·내비게이션에 영향을 주지 않습니다.
	CrateStoragePreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CrateStoragePreviewMesh->SetGenerateOverlapEvents(false);
	CrateStoragePreviewMesh->SetCanEverAffectNavigation(false);
	CrateStoragePreviewMesh->SetCastShadow(false);
	CrateStoragePreviewMesh->SetHiddenInGame(true);
	CrateStoragePreviewMesh->SetVisibility(false);
}

void ACSFreshnessSafeZoneActor::Interact_Implementation(AActor* Interactor)
{
	// 보관소의 적재는 플레이어가 손에 든 물건을 내려놓는 Q 입력에서만 처리합니다.
	// E 상호작용은 상자 슬롯의 회수처럼 별도 동작에만 사용하므로 여기서는 적재하지 않습니다.
}

FText ACSFreshnessSafeZoneActor::GetInteractionText_Implementation() const
{
	const APlayerController* PlayerController =
		GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	const ACSPlayerCharacter* PlayerCharacter =
		Cast<ACSPlayerCharacter>(PlayerController != nullptr ? PlayerController->GetPawn() : nullptr);
	const AActor* HeldActor = IsValid(PlayerCharacter) ? PlayerCharacter->GetHeldActor() : nullptr;

	if (const ACSDeliveryCrateActor* HeldCrate = Cast<ACSDeliveryCrateActor>(HeldActor))
	{
		return CanStoreCrate(HeldCrate)
			? FText::FromString(TEXT("상자 보관"))
			: FText::FromString(TEXT("이 창고에는 상자를 보관할 수 없습니다."));
	}

	if (const ACSProductPickupActor* HeldProduct = Cast<ACSProductPickupActor>(HeldActor))
	{
		return CanStoreLooseProduct(HeldProduct)
			? FText::FromString(TEXT("낱개 상품 보관"))
			: FText::FromString(TEXT("이 창고에는 상품을 보관할 수 없습니다."));
	}

	return FText::FromString(TEXT("창고: 상자 또는 상품을 드세요."));
}

bool ACSFreshnessSafeZoneActor::CanStoreCrate(const ACSDeliveryCrateActor* Crate) const
{
	return bAllowEmployeeStorage && IsValid(Crate) &&
		Crate->GetRemainingQuantity() > 0 && IsStorageTypeCompatible(Crate) &&
		FindEmptyStorageSlot() != INDEX_NONE;
}

bool ACSFreshnessSafeZoneActor::TryGetStorageApproachPoint(FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(StorageApproachPoint))
	{
		return false;
	}

	const UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World);
	FNavLocation ProjectedLocation;
	if (NavigationSystem == nullptr || !NavigationSystem->ProjectPointToNavigation(
		StorageApproachPoint->GetComponentLocation(),
		ProjectedLocation,
		FVector(100.0f, 100.0f, 300.0f)))
	{
		return false;
	}

	OutLocation = ProjectedLocation.Location;
	return true;
}

bool ACSFreshnessSafeZoneActor::TryStoreCrate(ACSDeliveryCrateActor* Crate)
{
	if (StoredCrates.Num() != CrateStorageSlotPoints.Num())
	{
		// BP에서 슬롯 수를 변경한 경우에도 점유 배열 길이를 맞춰 잘못된 인덱스 접근을 막습니다.
		StoredCrates.SetNum(CrateStorageSlotPoints.Num());
	}

	return StoreCrateAtSlot(Crate, FindEmptyStorageSlot());
}

bool ACSFreshnessSafeZoneActor::TryStoreCrateInSlot(
	ACSDeliveryCrateActor* Crate,
	const UPrimitiveComponent* SlotInteractionComponent)
{
	int32 SlotIndex = INDEX_NONE;
	return TryGetCrateStorageSlotIndex(SlotInteractionComponent, SlotIndex) &&
		StoreCrateAtSlot(Crate, SlotIndex);
}

bool ACSFreshnessSafeZoneActor::TryTakeCrateFromSlot(
	const UPrimitiveComponent* SlotInteractionComponent,
	ACSDeliveryCrateActor*& OutCrate)
{
	OutCrate = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!TryGetCrateStorageSlotIndex(SlotInteractionComponent, SlotIndex) ||
		!StoredCrates.IsValidIndex(SlotIndex) || !IsValid(StoredCrates[SlotIndex]))
	{
		return false;
	}

	OutCrate = StoredCrates[SlotIndex];
	StoredCrates[SlotIndex] = nullptr;
	// 플레이어가 손에 드는 즉시 창고의 냉장·냉동 보호 등록을 해제합니다.
	UnregisterOverlappingActor(OutCrate);
	return true;
}

bool ACSFreshnessSafeZoneActor::TryGetStoredCrateFromSlot(
	const UPrimitiveComponent* SlotInteractionComponent,
	ACSDeliveryCrateActor*& OutCrate) const
{
	OutCrate = nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (!TryGetCrateStorageSlotIndex(SlotInteractionComponent, SlotIndex) ||
		!StoredCrates.IsValidIndex(SlotIndex) || !IsValid(StoredCrates[SlotIndex]))
	{
		return false;
	}

	// 이 함수는 HUD 표시 전용입니다. 슬롯 점유와 신선도 보호 상태는 바꾸지 않습니다.
	OutCrate = StoredCrates[SlotIndex];
	return true;
}

bool ACSFreshnessSafeZoneActor::TryGetCrateStorageSlotIndex(
	const UPrimitiveComponent* SlotInteractionComponent,
	int32& OutSlotIndex) const
{
	OutSlotIndex = INDEX_NONE;
	const int32 FoundIndex = CrateStorageSlotInteractionBoxes.IndexOfByPredicate(
		[SlotInteractionComponent](const TObjectPtr<UBoxComponent>& InteractionBox)
		{
			return InteractionBox == SlotInteractionComponent;
		});
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	OutSlotIndex = FoundIndex;
	return true;
}

bool ACSFreshnessSafeZoneActor::TryGetCrateStorageSlotWorldTransform(
	const UPrimitiveComponent* SlotInteractionComponent,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	int32 SlotIndex = INDEX_NONE;
	if (!TryGetCrateStorageSlotIndex(SlotInteractionComponent, SlotIndex) ||
		!CrateStorageSlotPoints.IsValidIndex(SlotIndex) || !IsValid(CrateStorageSlotPoints[SlotIndex]))
	{
		return false;
	}

	OutTransform = CrateStorageSlotPoints[SlotIndex]->GetComponentTransform();
	return true;
}

bool ACSFreshnessSafeZoneActor::CanStoreCrateInSlot(
	const ACSDeliveryCrateActor* Crate,
	const UPrimitiveComponent* SlotInteractionComponent) const
{
	int32 SlotIndex = INDEX_NONE;
	return TryGetCrateStorageSlotIndex(SlotInteractionComponent, SlotIndex) &&
		CanStoreCrateAtIndex(Crate, SlotIndex);
}

bool ACSFreshnessSafeZoneActor::IsLooseProductDepositComponent(
	const UPrimitiveComponent* InteractionComponent) const
{
	return InteractionComponent == LooseProductDepositPoint;
}

bool ACSFreshnessSafeZoneActor::TryGetNextLooseProductStorageSlotWorldTransform(
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	const int32 SlotIndex = FindEmptyLooseProductStorageSlot();
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	OutTransform = GetLooseProductStorageSlotWorldTransform(SlotIndex);
	return true;
}

void ACSFreshnessSafeZoneActor::ShowCrateStoragePreview(
	const ACSDeliveryCrateActor* SourceCrate,
	const FTransform& SlotWorldTransform,
	bool bCanStore)
{
	const UStaticMeshComponent* SourceCrateMesh =
		IsValid(SourceCrate) ? SourceCrate->GetCrateMeshComponent() : nullptr;
	if (!IsValid(CrateStoragePreviewMesh) || !IsValid(SourceCrateMesh) ||
		SourceCrateMesh->GetStaticMesh() == nullptr)
	{
		HideCrateStoragePreview();
		return;
	}

	CrateStoragePreviewMesh->SetStaticMesh(SourceCrateMesh->GetStaticMesh());
	for (int32 MaterialIndex = 0;
		MaterialIndex < CrateStoragePreviewMesh->GetNumMaterials();
		++MaterialIndex)
	{
		CrateStoragePreviewMesh->SetMaterial(
			MaterialIndex,
			bCanStore && IsValid(ValidCrateStoragePreviewMaterial)
				? ValidCrateStoragePreviewMaterial.Get()
				: !bCanStore && IsValid(InvalidCrateStoragePreviewMaterial)
					? InvalidCrateStoragePreviewMaterial.Get()
				: SourceCrateMesh->GetMaterial(MaterialIndex));
	}

	// 상자 메시의 상대 보정까지 반영하여 실제 적재 결과와 고스트가 정확히 겹치게 합니다.
	CrateStoragePreviewMesh->SetWorldTransform(
		SourceCrateMesh->GetRelativeTransform() * SlotWorldTransform);
	CrateStoragePreviewMesh->SetHiddenInGame(false);
	CrateStoragePreviewMesh->SetVisibility(true);
}

void ACSFreshnessSafeZoneActor::HideCrateStoragePreview()
{
	if (IsValid(CrateStoragePreviewMesh))
	{
		CrateStoragePreviewMesh->SetHiddenInGame(true);
		CrateStoragePreviewMesh->SetVisibility(false);
	}
}

bool ACSFreshnessSafeZoneActor::CanStoreCrateAtIndex(
	const ACSDeliveryCrateActor* Crate,
	int32 SlotIndex) const
{
	return bAllowEmployeeStorage && IsValid(Crate) && Crate->GetRemainingQuantity() > 0 &&
		IsStorageTypeCompatible(Crate) &&
		SlotIndex >= 0 && SlotIndex < GetCrateStorageCapacity() &&
		(!StoredCrates.IsValidIndex(SlotIndex) || !IsValid(StoredCrates[SlotIndex])) &&
		CrateStorageSlotPoints.IsValidIndex(SlotIndex) &&
		IsValid(CrateStorageSlotPoints[SlotIndex]);
}

bool ACSFreshnessSafeZoneActor::StoreCrateAtSlot(ACSDeliveryCrateActor* Crate, int32 StorageSlotIndex)
{
	if (StoredCrates.Num() != CrateStorageSlotPoints.Num())
	{
		// 플레이어가 특정 슬롯을 먼저 조준한 경우에도 점유 배열을 Arrow 슬롯 수와 맞춥니다.
		StoredCrates.SetNum(CrateStorageSlotPoints.Num());
	}

	if (!CanStoreCrateAtIndex(Crate, StorageSlotIndex))
	{
		return false;
	}

	const FTransform SlotWorldTransform = CrateStorageSlotPoints[StorageSlotIndex]->GetComponentTransform();
	// 보관이 끝난 상자는 물리로 굴러가거나 직원과 충돌하지 않도록 슬롯 위치에 고정합니다.
	Crate->PrepareForPickupPhysics();
	Crate->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Crate->SetActorLocationAndRotation(
		SlotWorldTransform.GetLocation(),
		SlotWorldTransform.GetRotation());
	Crate->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	Crate->SetActorEnableCollision(false);
	Crate->SetHeldByPlayerForFreshness(false);
	// 어느 경로로 보관했든 실제 슬롯 적재가 성공한 시점에만 입고 대기를 끝냅니다.
	Crate->SetAwaitingStockroomIntake(false);
	StoredCrates[StorageSlotIndex] = Crate;

	// 저장 상자는 충돌을 끈 상태이므로, 겹침 이벤트에 의존하지 않고 냉장·냉동 안전 보관을 직접 등록합니다.
	RegisterOverlappingActor(Crate);
	return true;
}

bool ACSFreshnessSafeZoneActor::CanStoreLooseProduct(const ACSProductPickupActor* Product) const
{
	return bAllowEmployeeStorage && bAllowEmployeeLooseProductStorage && IsValid(Product) &&
		IsStorageTypeCompatible(Product) && FindEmptyLooseProductStorageSlot() != INDEX_NONE;
}

bool ACSFreshnessSafeZoneActor::TryStoreLooseProduct(ACSProductPickupActor* Product)
{
	if (StoredLooseProducts.Num() != GetLooseProductStorageCapacity())
	{
		// 보관 구역 크기나 간격을 바꾸어도 점유 배열이 자동으로 같은 수량을 유지하게 합니다.
		StoredLooseProducts.SetNum(GetLooseProductStorageCapacity());
	}

	const int32 StorageSlotIndex = FindEmptyLooseProductStorageSlot();
	if (!CanStoreLooseProduct(Product) || StorageSlotIndex == INDEX_NONE)
	{
		return false;
	}

	const FTransform SlotWorldTransform = GetLooseProductStorageSlotWorldTransform(StorageSlotIndex);
	// 낱개 상품은 보관 중 서로 밀리거나 구역 밖으로 굴러가지 않도록 물리를 해제해 자동 격자 위치에 고정합니다.
	Product->PrepareForPickupPhysics();
	Product->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Product->SetActorLocationAndRotation(SlotWorldTransform.GetLocation(), SlotWorldTransform.GetRotation());
	Product->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	// 낱개 상품은 물리 충돌 없이 Visibility Trace만 받게 하여, 창고 안에서 직접 조준해 꺼낼 수 있도록 합니다.
	Product->SetStoredForPlayerInteraction(true);
	Product->SetHeldByPlayerForFreshness(false);
	StoredLooseProducts[StorageSlotIndex] = Product;

	// 충돌을 끈 채 보관하므로 냉장·냉동 안전 보관 효과는 직접 등록합니다.
	RegisterOverlappingActor(Product);
	return true;
}

void ACSFreshnessSafeZoneActor::RemoveStoredLooseProduct(ACSProductPickupActor* Product)
{
	for (int32 StorageSlotIndex = 0; StorageSlotIndex < StoredLooseProducts.Num(); ++StorageSlotIndex)
	{
		if (StoredLooseProducts[StorageSlotIndex] == Product)
		{
			StoredLooseProducts[StorageSlotIndex] = nullptr;
			// 직원이 상품을 들기 시작하면 보관소의 신선도 보호를 즉시 해제합니다.
			UnregisterOverlappingActor(Product);
			return;
		}
	}
}

void ACSFreshnessSafeZoneActor::RemoveStoredCrate(ACSDeliveryCrateActor* Crate)
{
	for (int32 StorageSlotIndex = 0; StorageSlotIndex < StoredCrates.Num(); ++StorageSlotIndex)
	{
		if (StoredCrates[StorageSlotIndex] == Crate)
		{
			StoredCrates[StorageSlotIndex] = nullptr;
			// 직원이 상자를 다시 들면 더 이상 창고의 냉장·냉동 보호를 받지 않도록 즉시 해제합니다.
			UnregisterOverlappingActor(Crate);
			return;
		}
	}
}

void ACSFreshnessSafeZoneActor::BeginPlay()
{
	Super::BeginPlay();
	RefreshCrateStorageSlotAvailability();

	if (!IsSupportedSafeStorageType() && SafeStorageType != ECSProductShelfType::UtilityShelf)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FreshnessSafeZone] %s의 SafeStorageType은 냉장 또는 냉동 타입이 아니므로 효과를 적용하지 않습니다."),
			*GetName());
	}

	TArray<AActor*> InitiallyOverlappingActors;
	SafeZoneBox->GetOverlappingActors(InitiallyOverlappingActors);
	for (AActor* OverlappingActor : InitiallyOverlappingActors)
	{
		RegisterOverlappingActor(OverlappingActor);
	}
}

void ACSFreshnessSafeZoneActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const TArray<TWeakObjectPtr<AActor>> ActorsToUnregister = RegisteredActors.Array();
	for (const TWeakObjectPtr<AActor>& RegisteredActor : ActorsToUnregister)
	{
		if (AActor* Actor = RegisteredActor.Get())
		{
			UnregisterOverlappingActor(Actor);
		}
	}
	RegisteredActors.Reset();

	Super::EndPlay(EndPlayReason);
}

void ACSFreshnessSafeZoneActor::HandleSafeZoneBeginOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32,
	bool,
	const FHitResult&)
{
	RegisterOverlappingActor(OtherActor);
}

void ACSFreshnessSafeZoneActor::HandleSafeZoneEndOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32)
{
	UnregisterOverlappingActor(OtherActor);
}

bool ACSFreshnessSafeZoneActor::IsSupportedSafeStorageType() const
{
	return SafeStorageType == ECSProductShelfType::ChilledShelf ||
		SafeStorageType == ECSProductShelfType::FreezerShelf;
}

bool ACSFreshnessSafeZoneActor::IsStorageTypeCompatible(const ACSDeliveryCrateActor* Crate) const
{
	if (!IsValid(Crate))
	{
		return false;
	}

	return IsProductRowCompatible(Crate->GetProductRow());
}

bool ACSFreshnessSafeZoneActor::IsStorageTypeCompatible(const ACSProductPickupActor* Product) const
{
	return IsValid(Product) && IsProductRowCompatible(Product->GetProductRow());
}

bool ACSFreshnessSafeZoneActor::IsProductRowCompatible(const FDataTableRowHandle& ProductRow) const
{
	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("ACSFreshnessSafeZoneActor::IsStorageTypeCompatible"));
	if (ProductData == nullptr)
	{
		return false;
	}

	if (SafeStorageType == ECSProductShelfType::UtilityShelf)
	{
		// 일반 창고에는 냉장·냉동 상품을 넣지 않아 신선도 관리 규칙을 보존합니다.
		return ProductData->ShelfType != ECSProductShelfType::ChilledShelf &&
			ProductData->ShelfType != ECSProductShelfType::FreezerShelf;
	}

	return ProductData->ShelfType == SafeStorageType;
}

int32 ACSFreshnessSafeZoneActor::FindEmptyStorageSlot() const
{
	for (int32 SlotIndex = 0; SlotIndex < GetCrateStorageCapacity(); ++SlotIndex)
	{
		if (!StoredCrates.IsValidIndex(SlotIndex) || !IsValid(StoredCrates[SlotIndex]))
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

void ACSFreshnessSafeZoneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshCrateStorageSlotAvailability();
}

int32 ACSFreshnessSafeZoneActor::GetCrateStorageCapacity() const
{
	return FMath::Clamp(CrateStorageCapacity, 1, CrateStorageSlotPoints.Num());
}

void ACSFreshnessSafeZoneActor::RefreshCrateStorageSlotAvailability()
{
	const int32 ActiveSlotCount = GetCrateStorageCapacity();
	for (int32 SlotIndex = 0; SlotIndex < CrateStorageSlotInteractionBoxes.Num(); ++SlotIndex)
	{
		if (UBoxComponent* InteractionBox = CrateStorageSlotInteractionBoxes[SlotIndex])
		{
			// 비활성 슬롯은 조준·상호작용 대상이 되지 않으므로 용량보다 큰 슬롯에 상자를 넣을 수 없습니다.
			InteractionBox->SetCollisionEnabled(
				SlotIndex < ActiveSlotCount ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		}
	}
}

int32 ACSFreshnessSafeZoneActor::FindEmptyLooseProductStorageSlot() const
{
	for (int32 SlotIndex = 0; SlotIndex < GetLooseProductStorageCapacity(); ++SlotIndex)
	{
		if (!StoredLooseProducts.IsValidIndex(SlotIndex) || !IsValid(StoredLooseProducts[SlotIndex]) ||
			StoredLooseProducts[SlotIndex]->GetAttachParentActor() != this)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

int32 ACSFreshnessSafeZoneActor::GetLooseProductStorageCapacity() const
{
	if (!IsValid(LooseProductStorageZone))
	{
		return 0;
	}

	const FVector BoxExtent = LooseProductStorageZone->GetUnscaledBoxExtent();
	const int32 ColumnCount = FMath::Max(1, FMath::FloorToInt((BoxExtent.X * 2.0f) / LooseProductSlotSpacing.X));
	const int32 RowCount = FMath::Max(1, FMath::FloorToInt((BoxExtent.Y * 2.0f) / LooseProductSlotSpacing.Y));
	const int32 LayerCount = FMath::Max(1, FMath::FloorToInt((BoxExtent.Z * 2.0f) / LooseProductSlotSpacing.Z));
	return ColumnCount * RowCount * LayerCount;
}

FTransform ACSFreshnessSafeZoneActor::GetLooseProductStorageSlotWorldTransform(int32 SlotIndex) const
{
	if (!IsValid(LooseProductStorageZone) || SlotIndex < 0)
	{
		return FTransform::Identity;
	}

	const FVector BoxExtent = LooseProductStorageZone->GetUnscaledBoxExtent();
	const int32 ColumnCount = FMath::Max(1, FMath::FloorToInt((BoxExtent.X * 2.0f) / LooseProductSlotSpacing.X));
	const int32 RowCount = FMath::Max(1, FMath::FloorToInt((BoxExtent.Y * 2.0f) / LooseProductSlotSpacing.Y));
	const int32 LayerIndex = SlotIndex / (ColumnCount * RowCount);
	const int32 RowIndex = (SlotIndex / ColumnCount) % RowCount;
	const int32 ColumnIndex = SlotIndex % ColumnCount;

	const FVector LocalLocation(
		-BoxExtent.X + LooseProductSlotSpacing.X * (ColumnIndex + 0.5f),
		-BoxExtent.Y + LooseProductSlotSpacing.Y * (RowIndex + 0.5f),
		-BoxExtent.Z + LooseProductSlotSpacing.Z * (LayerIndex + 0.5f));
	return FTransform(
		LooseProductStorageZone->GetComponentQuat(),
		LooseProductStorageZone->GetComponentTransform().TransformPosition(LocalLocation));
}

void ACSFreshnessSafeZoneActor::RegisterOverlappingActor(AActor* OtherActor)
{
	if (!IsSupportedSafeStorageType() || !IsValid(OtherActor) ||
		RegisteredActors.Contains(OtherActor))
	{
		return;
	}

	if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(OtherActor))
	{
		RegisteredActors.Add(OtherActor);
		ProductActor->EnterFreshnessSafeZone(SafeStorageType);
	}
	else if (ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(OtherActor))
	{
		RegisteredActors.Add(OtherActor);
		DeliveryCrate->EnterFreshnessSafeZone(SafeStorageType);
	}
}

void ACSFreshnessSafeZoneActor::UnregisterOverlappingActor(AActor* OtherActor)
{
	if (!IsValid(OtherActor) || RegisteredActors.Remove(OtherActor) == 0)
	{
		return;
	}

	if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(OtherActor))
	{
		ProductActor->ExitFreshnessSafeZone(SafeStorageType);
	}
	else if (ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(OtherActor))
	{
		DeliveryCrate->ExitFreshnessSafeZone(SafeStorageType);
	}
}
