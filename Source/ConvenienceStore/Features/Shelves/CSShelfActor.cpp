#include "Features/Shelves/CSShelfActor.h"

#include "Components/ArrowComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSProductTypes.h"
#include "Features/Items/CSProductPickupActor.h"

ACSShelfActor::ACSShelfActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 가구 이동 시 바닥에 맞출 기준점과 실제 메시를 분리합니다.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 메시를 자식으로 두어 블루프린트마다 피벗 높이를 보정할 수 있게 합니다.
	ShelfMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShelfMesh"));
	ShelfMesh->SetupAttachment(SceneRoot);
	ShelfMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSShelfActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 시설 행을 먼저 읽어 두면 에디터 프리뷰와 실제 배치가 같은 보관 환경을 사용합니다.
	SyncFacilityData();
	// 블루프린트에서 배치한 슬롯 마커의 위치를 슬롯 데이터에 반영합니다.
	SyncSlotsFromMarkers();
}

void ACSShelfActor::BeginPlay()
{
	Super::BeginPlay();

	// 에디터가 아닌 실제 게임 시작 시에만 상품을 생성하여 중복 배치되지 않도록 합니다.
	SpawnInitialProducts();
}

FName ACSShelfActor::GetFacilityId_Implementation() const
{
	return FacilityRow.RowName;
}

bool ACSShelfActor::IsFacilityInstalled_Implementation() const
{
	return bIsInstalled;
}

void ACSShelfActor::InitializeAsUninstalledFacility_Implementation()
{
	bIsInstalled = false;
	SetFacilityPhysicsEnabled(true);
}

void ACSShelfActor::PrepareForBuildPickup_Implementation()
{
	SetFacilityPhysicsEnabled(false);
}

void ACSShelfActor::FinalizeBuildPlacement_Implementation()
{
	SetFacilityPhysicsEnabled(false);
	bIsInstalled = true;
}

void ACSShelfActor::SpawnInitialProducts()
{
	if (!bIsInstalled || InitialProducts.IsEmpty())
	{
		return;
	}

	if (InitialProductActorClass == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 시작 진열 상품을 생성할 액터 클래스가 지정되지 않았습니다."),
			*GetName());
		return;
	}

	for (const FCSInitialShelfProduct& InitialProduct : InitialProducts)
	{
		if (InitialProduct.ProductRow.DataTable == nullptr ||
			InitialProduct.ProductRow.RowName.IsNone())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: 시작 진열 상품의 DataTable 행이 비어 있습니다."),
				*GetName());
			continue;
		}

		const int32 RequestedQuantity = FMath::Max(1, InitialProduct.Quantity);
		int32 PlacedQuantity = 0;
		const int32 FirstSlotIndex = FMath::Max(0, InitialProduct.StartSlotIndex);
		for (int32 SlotIndex = FirstSlotIndex;
			SlotIndex < Slots.Num() && PlacedQuantity < RequestedQuantity;
			++SlotIndex)
		{
			// 이미 사용 중인 슬롯은 건너뛰고 다음 빈 슬롯을 계속 찾습니다.
			if (IsValid(Slots[SlotIndex].OccupyingItem))
			{
				continue;
			}

			if (SpawnInitialProductInSlot(InitialProduct.ProductRow, SlotIndex))
			{
				++PlacedQuantity;
			}
		}

		if (PlacedQuantity < RequestedQuantity)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: %s 상품의 시작 진열 수량이 부족한 슬롯으로 인해 일부만 배치되었습니다. 요청=%d, 배치=%d"),
				*GetName(),
				*InitialProduct.ProductRow.RowName.ToString(),
				RequestedQuantity,
				PlacedQuantity);
		}
	}
}

bool ACSShelfActor::SpawnInitialProductInSlot(
	const FDataTableRowHandle& ProductRow,
	int32 SlotIndex)
{
	if (GetWorld() == nullptr ||
		InitialProductActorClass == nullptr ||
		!Slots.IsValidIndex(SlotIndex) ||
		IsValid(Slots[SlotIndex].OccupyingItem))
	{
		return false;
	}

	const FTransform SlotWorldTransform =
		Slots[SlotIndex].LocalTransform * GetActorTransform();
	ACSProductPickupActor* ProductActor =
		GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
			InitialProductActorClass,
			SlotWorldTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(ProductActor) || !ProductActor->InitializeProduct(ProductRow))
	{
		if (IsValid(ProductActor))
		{
			ProductActor->Destroy();
		}

		return false;
	}

	ProductActor->FinishSpawning(SlotWorldTransform);
	if (!TryPlaceItem_Implementation(ProductActor, SlotWorldTransform.GetLocation()))
	{
		ProductActor->Destroy();
		return false;
	}

	return true;
}

void ACSShelfActor::SyncFacilityData()
{
	if (FacilityRow.DataTable == nullptr || FacilityRow.RowName.IsNone())
	{
		return;
	}

	const FCSFacilityDataRow* FacilityData =
		FacilityRow.GetRow<FCSFacilityDataRow>(TEXT("ACSShelfActor::SyncFacilityData"));
	if (FacilityData != nullptr && FacilityData->FacilityType == ECSFacilityType::Shelf)
	{
		ShelfType = FacilityData->ShelfType;
	}
}

bool ACSShelfActor::TryGetCustomerBrowseLocation(FVector& OutLocation) const
{
	TArray<UArrowComponent*> ArrowComponents;
	GetComponents<UArrowComponent>(ArrowComponents);

	static const FString BrowsePointName = TEXT("CustomerBrowsePoint");
	static const FString BrowsePointPrefix = BrowsePointName + TEXT("_");
	for (const UArrowComponent* ArrowComponent : ArrowComponents)
	{
		if (!IsValid(ArrowComponent))
		{
			continue;
		}

		const FString ComponentName = ArrowComponent->GetName();
		if (ComponentName == BrowsePointName || ComponentName.StartsWith(BrowsePointPrefix))
		{
			OutLocation = ArrowComponent->GetComponentLocation();
			return true;
		}
	}

	return false;
}

bool ACSShelfActor::TryGetSlotMarkerIndex(
	const UArrowComponent* SlotMarker,
	int32& OutSlotIndex) const
{
	if (!IsValid(SlotMarker))
	{
		return false;
	}

	const FString MarkerName = SlotMarker->GetName();
	static const FString SlotPrefix = TEXT("Slot_");
	// Slot_00처럼 정해진 접두사로 시작하는 Arrow Component만 슬롯 마커로 사용합니다.
	if (!MarkerName.StartsWith(SlotPrefix))
	{
		return false;
	}

	const FString IndexSuffix = MarkerName.RightChop(SlotPrefix.Len());
	FString IndexText;
	// 블루프린트가 내부 접미사를 덧붙여도 앞의 숫자만 슬롯 번호로 사용합니다.
	for (const TCHAR Character : IndexSuffix)
	{
		if (!FChar::IsDigit(Character))
		{
			break;
		}

		IndexText.AppendChar(Character);
	}

	if (IndexText.IsEmpty())
	{
		return false;
	}

	OutSlotIndex = FCString::Atoi(*IndexText);
	return OutSlotIndex >= 0;
}

void ACSShelfActor::SyncSlotsFromMarkers()
{
	TArray<UArrowComponent*> ArrowComponents;
	GetComponents<UArrowComponent>(ArrowComponents);

	TArray<TPair<int32, UArrowComponent*>> SlotMarkers;
	// 진열대에 포함된 Arrow Component 중 슬롯 이름 규칙을 만족하는 마커만 모읍니다.
	for (UArrowComponent* ArrowComponent : ArrowComponents)
	{
		int32 SlotIndex = INDEX_NONE;
		if (TryGetSlotMarkerIndex(ArrowComponent, SlotIndex))
		{
			SlotMarkers.Emplace(SlotIndex, ArrowComponent);
		}
	}

	// 마커는 선택 사항이므로, 기존 진열대는 수동으로 지정한 슬롯 값을 유지합니다.
	if (SlotMarkers.IsEmpty())
	{
		return;
	}

	SlotMarkers.Sort([](const TPair<int32, UArrowComponent*>& Left,
		const TPair<int32, UArrowComponent*>& Right)
	{
		return Left.Key < Right.Key;
	});

	// 슬롯 번호가 중복되거나 건너뛰면 잘못된 슬롯 배열이 만들어질 수 있어 동기화를 중단합니다.
	for (int32 ExpectedIndex = 0; ExpectedIndex < SlotMarkers.Num(); ++ExpectedIndex)
	{
		if (SlotMarkers[ExpectedIndex].Key != ExpectedIndex)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: 슬롯 마커는 Slot_00부터 번호를 건너뛰지 않고 순서대로 이름을 지정해 주세요."),
				*GetName());
			return;
		}
	}

	Slots.SetNum(SlotMarkers.Num());
	// 이름순으로 정렬된 마커의 상대 위치와 회전을 기존 슬롯 데이터에 복사합니다.
	for (int32 SlotIndex = 0; SlotIndex < SlotMarkers.Num(); ++SlotIndex)
	{
		UArrowComponent* SlotMarker = SlotMarkers[SlotIndex].Value;
		Slots[SlotIndex].LocalTransform = SlotMarker->GetRelativeTransform();
		// 마커는 블루프린트 편집 전용이므로 게임 화면에서는 보이지 않게 설정합니다.
		SlotMarker->SetHiddenInGame(true);
	}
}

bool ACSShelfActor::CanPlaceItem_Implementation(AActor* ItemActor) const
{
	return bIsInstalled &&
		IsValid(ItemActor) &&
		Slots.ContainsByPredicate([](const FCSShelfSlot& Slot)
		{
			return !IsValid(Slot.OccupyingItem);
		});
}

void ACSShelfActor::SetPlacementHighlight(
	ECSShelfPlacementPreviewState NewState)
{
	if (NewState == ECSShelfPlacementPreviewState::Invalid)
	{
		RestorePlacementHighlight();
		return;
	}

	// Post Process 외곽선 머티리얼에서 1은 초록, 2는 노랑으로 설정하여 사용합니다.
	const int32 StencilValue = NewState == ECSShelfPlacementPreviewState::Warning
		? 2
		: 1;

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		// 최초 표시 시의 값만 저장하여 외곽선 해제 후 원래 설정으로 되돌립니다.
		if (!OriginalCustomDepthStates.Contains(PrimitiveComponent))
		{
			OriginalCustomDepthStates.Add(
				PrimitiveComponent,
				PrimitiveComponent->bRenderCustomDepth);
			OriginalCustomStencilValues.Add(
				PrimitiveComponent,
				PrimitiveComponent->CustomDepthStencilValue);
		}

		PrimitiveComponent->SetRenderCustomDepth(true);
		PrimitiveComponent->SetCustomDepthStencilValue(StencilValue);
	}
}

void ACSShelfActor::RestorePlacementHighlight()
{
	for (const TPair<TObjectPtr<UPrimitiveComponent>, bool>& Pair : OriginalCustomDepthStates)
	{
		UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get();
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		PrimitiveComponent->SetRenderCustomDepth(Pair.Value);
		if (const int32* OriginalStencilValue =
			OriginalCustomStencilValues.Find(PrimitiveComponent))
		{
			PrimitiveComponent->SetCustomDepthStencilValue(*OriginalStencilValue);
		}
	}

	OriginalCustomDepthStates.Empty();
	OriginalCustomStencilValues.Empty();
}

int32 ACSShelfActor::FindClosestEmptySlot(FVector PlacementHint) const
{
	int32 ClosestSlotIndex = INDEX_NONE;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const FCSShelfSlot& Slot = Slots[SlotIndex];
		if (IsValid(Slot.OccupyingItem))
		{
			continue;
		}

		const FTransform SlotWorldTransform =
			Slot.LocalTransform * GetActorTransform();
		const float DistanceSquared = FVector::DistSquared(
			SlotWorldTransform.GetLocation(),
			PlacementHint);

		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestSlotIndex = SlotIndex;
		}
	}

	return ClosestSlotIndex;
}

bool ACSShelfActor::GetPlacementPreview_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	const int32 SlotIndex = FindClosestEmptySlot(PlacementHint);
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	OutSlotWorldTransform =
		Slots[SlotIndex].LocalTransform * GetActorTransform();
	return true;
}

ECSShelfPlacementPreviewState ACSShelfActor::GetPlacementPreviewState_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	if (!GetPlacementPreview_Implementation(ItemActor, PlacementHint, OutSlotWorldTransform))
	{
		return ECSShelfPlacementPreviewState::Invalid;
	}

	// 모든 상품은 진열할 수 있으므로 보관 조건이 맞지 않아도 배치를 막지 않고 경고만 반환합니다.
	return ShouldWarnStorage(ItemActor)
		? ECSShelfPlacementPreviewState::Warning
		: ECSShelfPlacementPreviewState::Valid;
}

bool ACSShelfActor::TryPlaceItem_Implementation(
	AActor* ItemActor,
	FVector PlacementHint)
{
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	const int32 SlotIndex = FindClosestEmptySlot(PlacementHint);
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	FCSShelfSlot& SelectedSlot = Slots[SlotIndex];
	const FTransform SlotWorldTransform =
		SelectedSlot.LocalTransform * GetActorTransform();

	ItemActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ItemActor->SetActorLocationAndRotation(
		SlotWorldTransform.GetLocation(),
		SlotWorldTransform.GetRotation());
	ItemActor->AttachToActor(
		this,
		FAttachmentTransformRules::KeepWorldTransform);
	ItemActor->SetActorEnableCollision(true);

	if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(ItemActor))
	{
		// 배치된 시설의 보관 환경을 상품에 기록하여 날짜가 바뀔 때 변질 속도를 계산합니다.
		ProductActor->SetStorageShelfType(ShelfType);
	}

	SelectedSlot.OccupyingItem = ItemActor;

	// 슬롯에 배치가 완료된 시점에만 방송
	OnItemPlaced.Broadcast(ItemActor, SlotIndex);

	return true;
}

void ACSShelfActor::RemovePlacedItem_Implementation(AActor* ItemActor)
{
	for (FCSShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem == ItemActor)
		{
			if (ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(ItemActor))
			{
				// 진열대에서 꺼낸 상품은 일반 환경으로 간주해 부적절한 보관 여부를 다시 계산합니다.
				ProductActor->SetStorageShelfType(ECSProductShelfType::UtilityShelf);
			}
			Slot.OccupyingItem = nullptr;
			return;
		}
	}
}

bool ACSShelfActor::ShouldWarnStorage(AActor* ItemActor) const
{
	const ACSProductPickupActor* ProductActor = Cast<ACSProductPickupActor>(ItemActor);
	if (!IsValid(ProductActor))
	{
		return false;
	}

	const FCSProductDataRow* ProductData =
		ProductActor->GetProductRow().GetRow<FCSProductDataRow>(TEXT("ACSShelfActor::ShouldWarnStorage"));
	if (ProductData == nullptr)
	{
		return false;
	}

	// 회의 규칙에 따라 냉장·냉동 상품만 경고 대상으로 두고, 나머지 상품은 모든 진열대에 정상 배치합니다.
	if (ProductData->ShelfType == ECSProductShelfType::ChilledShelf)
	{
		return ShelfType != ECSProductShelfType::ChilledShelf;
	}

	if (ProductData->ShelfType == ECSProductShelfType::FreezerShelf)
	{
		return ShelfType != ECSProductShelfType::FreezerShelf;
	}

	return false;
}

void ACSShelfActor::SetFacilityPhysicsEnabled(bool bEnabled)
{
	if (!IsValid(ShelfMesh))
	{
		return;
	}

	if (bEnabled)
	{
		// Blueprint에서 조정한 메시 피벗과 높이를 물리가 분리하기 전에 그대로 저장합니다.
		if (!ShelfMesh->IsSimulatingPhysics() &&
			ShelfMesh->GetAttachParent() == SceneRoot)
		{
			ShelfMeshRelativeTransformBeforePhysics = ShelfMesh->GetRelativeTransform();
			bHasCachedShelfMeshRelativeTransform = true;
		}

		ShelfMesh->SetMobility(EComponentMobility::Movable);
		ShelfMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ShelfMesh->SetEnableGravity(true);
		ShelfMesh->SetSimulatePhysics(true);
		if (ShelfMesh->IsSimulatingPhysics())
		{
			ShelfMesh->WakeAllRigidBodies();
		}
		return;
	}

	if (ShelfMesh->IsSimulatingPhysics())
	{
		ShelfMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ShelfMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		ShelfMesh->PutAllRigidBodiesToSleep();
	}
	ShelfMesh->SetSimulatePhysics(false);
	ShelfMesh->SetEnableGravity(false);

	bool bAttachedToSceneRoot = ShelfMesh->GetAttachParent() == SceneRoot;
	if (!bAttachedToSceneRoot && IsValid(SceneRoot))
	{
		bAttachedToSceneRoot = ShelfMesh->AttachToComponent(
			SceneRoot,
			FAttachmentTransformRules::KeepWorldTransform);
	}

	if (bAttachedToSceneRoot && bHasCachedShelfMeshRelativeTransform)
	{
		ShelfMesh->SetRelativeTransform(
			ShelfMeshRelativeTransformBeforePhysics,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}
}

bool ACSShelfActor::GetItemSlotLevel_Implementation(
	AActor* ItemActor,
	ECSShelfSlotLevel& OutSlotLevel) const
{
	// 슬롯을 찾지 못한 경우에도 호출 측에서 안전하게 사용할 수 있도록 중단을 기본값으로 설정합니다.
	OutSlotLevel = ECSShelfSlotLevel::Middle;

	if (!IsValid(ItemActor))
	{
		return false;
	}

	// 상품 액터와 일치하는 점유 슬롯을 찾아 해당 슬롯에 설정된 높이 정보를 반환합니다.
	for (const FCSShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem == ItemActor)
		{
			OutSlotLevel = Slot.SlotLevel;
			return true;
		}
	}

	return false;
}
