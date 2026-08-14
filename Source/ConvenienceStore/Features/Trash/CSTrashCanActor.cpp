#include "Features/Trash/CSTrashCanActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

ACSTrashCanActor::ACSTrashCanActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TrashCanMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrashCanMesh"));
	TrashCanMesh->SetupAttachment(SceneRoot);
	TrashCanMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

bool ACSTrashCanActor::CanPlaceItem_Implementation(AActor* ItemActor) const
{
	// 지금은 무엇이든 버릴 수 있게 허용
	// 나중에 상품이 여기 버려지면 매입가만큼 매출 손실로 기록하면 실수로 버리는 것도 플레이어의 실수로 이어지는 재미 요소가 될 수 있음
	return IsValid(ItemActor);
}

bool ACSTrashCanActor::GetPlacementPreview_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	// 슬롯이라는 개념이 없어 조준한 지점에 고스트가 표시되도록 그대로 반환
	OutSlotWorldTransform = FTransform(GetActorRotation(), PlacementHint);
	return true;
}

ECSShelfPlacementPreviewState ACSTrashCanActor::GetPlacementPreviewState_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	if (!GetPlacementPreview_Implementation(ItemActor, PlacementHint, OutSlotWorldTransform))
	{
		return ECSShelfPlacementPreviewState::Invalid;
	}

	// 보관 환경 경고 같은 개념이 없으므로 항상 정상(Valid)으로 표시
	return ECSShelfPlacementPreviewState::Valid;
}

bool ACSTrashCanActor::TryPlaceItem_Implementation(AActor* ItemActor, FVector PlacementHint)
{
	if (!CanPlaceItem_Implementation(ItemActor))
	{
		return false;
	}

	if (TotalDisposedCount < MAX_int32)
	{
		++TotalDisposedCount;
	}

	// 파괴하기 전에 먼저 방송
	OnItemDisposed.Broadcast(ItemActor, TotalDisposedCount);

	ItemActor->Destroy();
	return true;
}

void ACSTrashCanActor::RemovePlacedItem_Implementation(AActor* ItemActor)
{
	// 파괴와 동시에 처리가 끝나므로 되돌릴 점유 슬롯이 없음
}

bool ACSTrashCanActor::GetItemSlotLevel_Implementation(
	AActor* ItemActor,
	ECSShelfSlotLevel& OutSlotLevel) const
{
	// 손님 AI 등이 잘못 호출해도 안전하게 실패를 반환
	OutSlotLevel = ECSShelfSlotLevel::Middle;
	return false;
}
