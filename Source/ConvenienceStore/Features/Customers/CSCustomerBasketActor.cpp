#include "Features/Customers/CSCustomerBasketActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Items/CSProductPickupActor.h"

ACSCustomerBasketActor::ACSCustomerBasketActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BasketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BasketMesh"));
	BasketMesh->SetupAttachment(SceneRoot);
	BasketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool ACSCustomerBasketActor::AddDisplayProduct(const FCSSelectProduct& SelectedProduct)
{
	if (GetWorld() == nullptr ||
		SelectedProduct.ProductClass == nullptr ||
		SelectedProduct.ProductRow.DataTable == nullptr ||
		SelectedProduct.ProductRow.RowName.IsNone())
	{
		return false;
	}
	UClass* ProductClass = SelectedProduct.ProductClass.Get();
	if (!IsValid(ProductClass) || !ProductClass->IsChildOf(ACSProductPickupActor::StaticClass()))
	{
		return false;
	}

	CollectDisplaySlots();
	if (!DisplaySlots.IsValidIndex(DisplayProducts.Num()) ||
		!IsValid(DisplaySlots[DisplayProducts.Num()]))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 장바구니 표시 슬롯이 부족하여 %s 상품을 표시하지 못했습니다."),
			*GetName(),
			*SelectedProduct.ProductRow.RowName.ToString());
		return false;
	}

	UArrowComponent* DisplaySlot = DisplaySlots[DisplayProducts.Num()];
	const FTransform SpawnTransform = DisplaySlot->GetComponentTransform();
	ACSProductPickupActor* DisplayProduct =
		GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
			ProductClass,
			SpawnTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(DisplayProduct) ||
		!DisplayProduct->InitializeProduct(SelectedProduct.ProductRow))
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->Destroy();
		}
		return false;
	}

	DisplayProduct->SetFreshnessFrozenForDisplay(true);
	DisplayProduct->ApplyProductRuntimeState(SelectedProduct.ProductRuntimeState);
	DisplayProduct->FinishSpawning(SpawnTransform);
	DisplayProduct->PrepareForPickupPhysics();
	DisplayProduct->SetActorEnableCollision(false);
	// 장바구니 상품은 화면 표시 전용이므로 실제 매장 재고에 포함하지 않습니다.
	DisplayProduct->SetExcludedFromStockCount(true);
	DisplayProduct->AttachToComponent(
		DisplaySlot,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	DisplayProducts.Add(DisplayProduct);
	return true;
}

void ACSCustomerBasketActor::ClearDisplayProducts()
{
	for (ACSProductPickupActor* DisplayProduct : DisplayProducts)
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->Destroy();
		}
	}
	DisplayProducts.Reset();
}

void ACSCustomerBasketActor::SetBasketDisplayVisible(bool bShouldBeVisible)
{
	if (IsValid(BasketMesh))
	{
		BasketMesh->SetHiddenInGame(!bShouldBeVisible);
	}

	for (ACSProductPickupActor* DisplayProduct : DisplayProducts)
	{
		if (IsValid(DisplayProduct))
		{
			DisplayProduct->SetActorHiddenInGame(!bShouldBeVisible);
		}
	}
}

void ACSCustomerBasketActor::CollectDisplaySlots()
{
	if (!DisplaySlots.IsEmpty())
	{
		return;
	}

	TArray<UArrowComponent*> ArrowComponents;
	GetComponents<UArrowComponent>(ArrowComponents);
	TArray<TPair<int32, UArrowComponent*>> IndexedSlots;
	for (UArrowComponent* ArrowComponent : ArrowComponents)
	{
		int32 SlotIndex = INDEX_NONE;
		if (TryGetDisplaySlotIndex(ArrowComponent, SlotIndex))
		{
			IndexedSlots.Emplace(SlotIndex, ArrowComponent);
		}
	}
	IndexedSlots.Sort([](const TPair<int32, UArrowComponent*>& Left,
		const TPair<int32, UArrowComponent*>& Right)
	{
		return Left.Key < Right.Key;
	});
	for (const TPair<int32, UArrowComponent*>& IndexedSlot : IndexedSlots)
	{
		DisplaySlots.Add(IndexedSlot.Value);
		IndexedSlot.Value->SetHiddenInGame(true);
	}
}

bool ACSCustomerBasketActor::TryGetDisplaySlotIndex(
	const UArrowComponent* SlotMarker,
	int32& OutSlotIndex) const
{
	if (!IsValid(SlotMarker))
	{
		return false;
	}

	const FString Prefix = TEXT("BasketSlot_");
	const FString MarkerName = SlotMarker->GetName();
	if (!MarkerName.StartsWith(Prefix))
	{
		return false;
	}

	const FString IndexText = MarkerName.RightChop(Prefix.Len());
	if (IndexText.IsEmpty() || !IndexText.IsNumeric())
	{
		return false;
	}
	OutSlotIndex = FCString::Atoi(*IndexText);
	return OutSlotIndex >= 0;
}
