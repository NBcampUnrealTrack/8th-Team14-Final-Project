#include "Features/Cart/CSCartActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACSCartActor::ACSCartActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 기존 상품 진열 프리뷰와 동일한 초록 반투명 머티리얼을 기본으로 사용합니다.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ValidPreviewMaterialFinder(
		TEXT("/Game/ConvenienceStore/Asset/ProductAsset/Material/MI_CSPlacementPreview_Valid.MI_CSPlacementPreview_Valid"));
	if (ValidPreviewMaterialFinder.Succeeded())
	{
		ValidCratePreviewMaterial = ValidPreviewMaterialFinder.Object;
	}

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CartMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CartMesh"));
	CartMesh->SetupAttachment(SceneRoot);
	CartMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	HandleAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("HandleAnchor"));
	HandleAnchor->SetupAttachment(SceneRoot);

	CratePlacementPreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("CratePlacementPreviewMesh"));
	CratePlacementPreviewMesh->SetupAttachment(SceneRoot);
	// 고스트 프리뷰는 상호작용·물리·내비게이션 대상이 아니어야 합니다.
	CratePlacementPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CratePlacementPreviewMesh->SetGenerateOverlapEvents(false);
	CratePlacementPreviewMesh->SetCanEverAffectNavigation(false);
	CratePlacementPreviewMesh->SetCastShadow(false);
	CratePlacementPreviewMesh->SetHiddenInGame(true);
	CratePlacementPreviewMesh->SetVisibility(false);
}

void ACSCartActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SyncCargoSlotsFromMarkers();
}

float ACSCartActor::GetSpawnGroundOffset() const
{
	if (!IsValid(CartMesh) || !IsValid(CartMesh->GetStaticMesh())) return 5.0f;
	const FBoxSphereBounds Bounds = GetSpawnMeshBounds();
	return FMath::Max(5.0f, Bounds.BoxExtent.Z - Bounds.Origin.Z + 5.0f);
}

FBoxSphereBounds ACSCartActor::GetSpawnMeshBounds() const
{
	return IsValid(CartMesh) && IsValid(CartMesh->GetStaticMesh())
		? CartMesh->CalcBounds(CartMesh->GetRelativeTransform())
		: FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0.0f);
}

bool ACSCartActor::CanPlaceItem_Implementation(AActor* ItemActor) const
{
	return !bIsBeingPushed &&
		Cast<ACSDeliveryCrateActor>(ItemActor) != nullptr &&
		CargoSlots.ContainsByPredicate([](const FCSCartCargoSlot& Slot)
		{
			return !IsValid(Slot.OccupyingCrate);
		});
}

bool ACSCartActor::GetPlacementPreview_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	const int32 SlotIndex = CanPlaceItem_Implementation(ItemActor)
		? FindClosestEmptyCargoSlot(PlacementHint)
		: INDEX_NONE;
	if (!CargoSlots.IsValidIndex(SlotIndex))
	{
		return false;
	}

	OutSlotWorldTransform = CargoSlots[SlotIndex].LocalTransform * GetActorTransform();
	return true;
}

ECSShelfPlacementPreviewState ACSCartActor::GetPlacementPreviewState_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	return GetPlacementPreview_Implementation(ItemActor, PlacementHint, OutSlotWorldTransform)
		? ECSShelfPlacementPreviewState::Valid
		: ECSShelfPlacementPreviewState::Invalid;
}

bool ACSCartActor::TryPlaceItem_Implementation(AActor* ItemActor, FVector PlacementHint)
{
	ACSDeliveryCrateActor* DeliveryCrate = Cast<ACSDeliveryCrateActor>(ItemActor);
	const int32 SlotIndex = CanPlaceItem_Implementation(DeliveryCrate)
		? FindClosestEmptyCargoSlot(PlacementHint)
		: INDEX_NONE;
	if (!IsValid(DeliveryCrate) || !CargoSlots.IsValidIndex(SlotIndex))
	{
		return false;
	}

	FCSCartCargoSlot& CargoSlot = CargoSlots[SlotIndex];
	const FTransform SlotWorldTransform = CargoSlot.LocalTransform * GetActorTransform();

	// 손에 들던 상자의 물리를 해제한 뒤, 수레 적재 칸 위치에 정확히 고정합니다.
	DeliveryCrate->PrepareForPickupPhysics();
	DeliveryCrate->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	DeliveryCrate->SetActorTransform(SlotWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	DeliveryCrate->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	DeliveryCrate->SetActorEnableCollision(true);
	DeliveryCrate->SetHeldByPlayerForFreshness(false);
	CargoSlot.OccupyingCrate = DeliveryCrate;
	return true;
}

void ACSCartActor::RemovePlacedItem_Implementation(AActor* ItemActor)
{
	for (FCSCartCargoSlot& CargoSlot : CargoSlots)
	{
		if (CargoSlot.OccupyingCrate == ItemActor)
		{
			// E로 상자를 다시 들면 해당 적재 칸을 즉시 비웁니다.
			CargoSlot.OccupyingCrate = nullptr;
			return;
		}
	}
}

bool ACSCartActor::GetItemSlotLevel_Implementation(
	AActor* ItemActor,
	ECSShelfSlotLevel& OutSlotLevel) const
{
	OutSlotLevel = ECSShelfSlotLevel::Middle;
	return CargoSlots.ContainsByPredicate([ItemActor](const FCSCartCargoSlot& Slot)
	{
		return Slot.OccupyingCrate == ItemActor;
	});
}

bool ACSCartActor::BeginPushing(AActor* NewPushingActor)
{
	if (!IsValid(NewPushingActor) || bIsBeingPushed)
	{
		return false;
	}

	PushingActor = NewPushingActor;
	bIsBeingPushed = true;
	if (IsValid(CartMesh))
	{
		// 밀고 있는 플레이어만 통과시키고, 벽과 문에 대한 수레 충돌은 그대로 유지합니다.
		PreviousPawnCollisionResponse = CartMesh->GetCollisionResponseToChannel(ECC_Pawn);
		CartMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	return true;
}

void ACSCartActor::StopPushing(AActor* RequestingActor)
{
	if (!bIsBeingPushed || (IsValid(PushingActor) && PushingActor != RequestingActor))
	{
		return;
	}

	if (IsValid(CartMesh))
	{
		CartMesh->SetCollisionResponseToChannel(ECC_Pawn, PreviousPawnCollisionResponse);
	}
	PushingActor = nullptr;
	bIsBeingPushed = false;
}

void ACSCartActor::UpdatePushTransform(const USceneComponent* PushAnchor)
{
	if (!bIsBeingPushed || !IsValid(PushAnchor) || !IsValid(HandleAnchor))
	{
		return;
	}

	FRotator DesiredRotation = PushAnchor->GetComponentRotation();
	DesiredRotation.Pitch = 0.0f;
	DesiredRotation.Roll = 0.0f;
	const FTransform DesiredHandleTransform(DesiredRotation, PushAnchor->GetComponentLocation());
	// HandleAnchor의 상대 Transform을 역으로 적용해 수레 본체가 손잡이 기준점에 맞도록 계산합니다.
	const FTransform DesiredActorTransform =
		HandleAnchor->GetRelativeTransform().Inverse() * DesiredHandleTransform;
	// Sweep 이동으로 벽·문·바닥과의 충돌을 유지하여 수레가 통과하지 않게 합니다.
	SetActorLocationAndRotation(
		DesiredActorTransform.GetLocation(),
		DesiredActorTransform.GetRotation(),
		true,
		nullptr,
		ETeleportType::None);
}

void ACSCartActor::ShowCratePlacementPreview(
	const ACSDeliveryCrateActor* SourceCrate,
	const FTransform& SlotWorldTransform)
{
	const UStaticMeshComponent* SourceCrateMesh =
		IsValid(SourceCrate) ? SourceCrate->GetCrateMeshComponent() : nullptr;
	if (!IsValid(CratePlacementPreviewMesh) || !IsValid(SourceCrateMesh) ||
		SourceCrateMesh->GetStaticMesh() == nullptr)
	{
		HideCratePlacementPreview();
		return;
	}

	CratePlacementPreviewMesh->SetStaticMesh(SourceCrateMesh->GetStaticMesh());
	for (int32 MaterialIndex = 0;
		MaterialIndex < CratePlacementPreviewMesh->GetNumMaterials();
		++MaterialIndex)
	{
		// 프리뷰 머티리얼이 없으면 원래 상자 재질을 사용해도 위치 확인은 가능하게 합니다.
		CratePlacementPreviewMesh->SetMaterial(
			MaterialIndex,
			IsValid(ValidCratePreviewMaterial)
				? ValidCratePreviewMaterial.Get()
				: SourceCrateMesh->GetMaterial(MaterialIndex));
	}

	// 상자 메시의 기본 상대 보정까지 복사하여 실제 적재 결과와 프리뷰 위치가 일치하게 합니다.
	const FTransform PreviewWorldTransform =
		SourceCrateMesh->GetRelativeTransform() * SlotWorldTransform;
	CratePlacementPreviewMesh->SetWorldTransform(PreviewWorldTransform);
	CratePlacementPreviewMesh->SetHiddenInGame(false);
	CratePlacementPreviewMesh->SetVisibility(true);
}

void ACSCartActor::HideCratePlacementPreview()
{
	if (IsValid(CratePlacementPreviewMesh))
	{
		CratePlacementPreviewMesh->SetHiddenInGame(true);
		CratePlacementPreviewMesh->SetVisibility(false);
	}
}

bool ACSCartActor::TryGetCargoSlotMarkerIndex(
	const UArrowComponent* SlotMarker,
	int32& OutSlotIndex) const
{
	if (!IsValid(SlotMarker))
	{
		return false;
	}

	static const FString SlotPrefix = TEXT("CartSlot_");
	const FString MarkerName = SlotMarker->GetName();
	if (!MarkerName.StartsWith(SlotPrefix))
	{
		return false;
	}

	FString IndexText;
	for (const TCHAR Character : MarkerName.RightChop(SlotPrefix.Len()))
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

void ACSCartActor::SyncCargoSlotsFromMarkers()
{
	TArray<UArrowComponent*> ArrowComponents;
	GetComponents<UArrowComponent>(ArrowComponents);

	TArray<TPair<int32, UArrowComponent*>> SlotMarkers;
	for (UArrowComponent* ArrowComponent : ArrowComponents)
	{
		int32 SlotIndex = INDEX_NONE;
		if (TryGetCargoSlotMarkerIndex(ArrowComponent, SlotIndex))
		{
			SlotMarkers.Emplace(SlotIndex, ArrowComponent);
		}
	}

	if (SlotMarkers.IsEmpty())
	{
		return;
	}

	SlotMarkers.Sort([](const TPair<int32, UArrowComponent*>& Left,
		const TPair<int32, UArrowComponent*>& Right)
	{
		return Left.Key < Right.Key;
	});

	for (int32 ExpectedIndex = 0; ExpectedIndex < SlotMarkers.Num(); ++ExpectedIndex)
	{
		if (SlotMarkers[ExpectedIndex].Key != ExpectedIndex)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: 수레 적재 칸은 CartSlot_00부터 번호를 건너뛰지 않고 지정해 주세요."),
				*GetName());
			return;
		}
	}

	TArray<TObjectPtr<AActor>> ExistingCrates;
	ExistingCrates.Reserve(CargoSlots.Num());
	for (const FCSCartCargoSlot& CargoSlot : CargoSlots)
	{
		ExistingCrates.Add(CargoSlot.OccupyingCrate);
	}
	CargoSlots.SetNum(SlotMarkers.Num());
	for (int32 SlotIndex = 0; SlotIndex < SlotMarkers.Num(); ++SlotIndex)
	{
		CargoSlots[SlotIndex].LocalTransform = SlotMarkers[SlotIndex].Value->GetRelativeTransform();
		CargoSlots[SlotIndex].OccupyingCrate = ExistingCrates.IsValidIndex(SlotIndex)
			? ExistingCrates[SlotIndex]
			: nullptr;
		// 슬롯 Arrow는 에디터 배치 기준점이므로 플레이 중에는 표시하지 않습니다.
		SlotMarkers[SlotIndex].Value->SetHiddenInGame(true);
	}
}

int32 ACSCartActor::FindClosestEmptyCargoSlot(FVector PlacementHint) const
{
	int32 ClosestSlotIndex = INDEX_NONE;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 SlotIndex = 0; SlotIndex < CargoSlots.Num(); ++SlotIndex)
	{
		const FCSCartCargoSlot& CargoSlot = CargoSlots[SlotIndex];
		if (IsValid(CargoSlot.OccupyingCrate))
		{
			continue;
		}

		const FTransform SlotWorldTransform = CargoSlot.LocalTransform * GetActorTransform();
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
