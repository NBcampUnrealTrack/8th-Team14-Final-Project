#include "Features/Shelves/CSShelfActor.h"

#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"

ACSShelfActor::ACSShelfActor()
{
	PrimaryActorTick.bCanEverTick = false;

	ShelfMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShelfMesh"));
	SetRootComponent(ShelfMesh);
	ShelfMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSShelfActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 블루프린트에서 배치한 슬롯 마커의 위치를 슬롯 데이터에 반영합니다.
	SyncSlotsFromMarkers();
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
	return IsValid(ItemActor) &&
		Slots.ContainsByPredicate([](const FCSShelfSlot& Slot)
		{
			return !IsValid(Slot.OccupyingItem);
		});
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

	SelectedSlot.OccupyingItem = ItemActor;
	return true;
}

void ACSShelfActor::RemovePlacedItem_Implementation(AActor* ItemActor)
{
	for (FCSShelfSlot& Slot : Slots)
	{
		if (Slot.OccupyingItem == ItemActor)
		{
			Slot.OccupyingItem = nullptr;
			return;
		}
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
