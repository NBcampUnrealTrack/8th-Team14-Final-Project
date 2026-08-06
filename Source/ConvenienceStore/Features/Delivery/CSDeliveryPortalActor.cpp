#include "Features/Delivery/CSDeliveryPortalActor.h"

#include "Components/SceneComponent.h"
#include "Data/CSProductTypes.h"
#include "Engine/World.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Systems/Ordering/CSOrderSubsystem.h"

ACSDeliveryPortalActor::ACSDeliveryPortalActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DeliverySpawnOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("DeliverySpawnOrigin"));
	DeliverySpawnOrigin->SetupAttachment(SceneRoot);
}

void ACSDeliveryPortalActor::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UCSOrderSubsystem* OrderSubsystem = World->GetSubsystem<UCSOrderSubsystem>())
		{
			OrderSubsystem->RegisterDeliveryPortal(this);
		}
	}
}

void ACSDeliveryPortalActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSOrderSubsystem* OrderSubsystem = World->GetSubsystem<UCSOrderSubsystem>())
		{
			OrderSubsystem->UnregisterDeliveryPortal(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool ACSDeliveryPortalActor::IsDeliveryConfigured() const
{
	return IsValid(DeliverySpawnOrigin) &&
		DeliveryCrateClass != nullptr &&
		GetWorld() != nullptr &&
		CratesPerRow >= 1;
}

bool ACSDeliveryPortalActor::SpawnOrderCrates(
	const TArray<FCSOrderLine>& OrderLines,
	TArray<ACSDeliveryCrateActor*>& OutSpawnedCrates)
{
	OutSpawnedCrates.Reset();
	if (!IsDeliveryConfigured())
	{
		return false;
	}

	for (const FCSOrderLine& OrderLine : OrderLines)
	{
		if (OrderLine.CrateCount == 0)
		{
			continue;
		}

		if (OrderLine.CrateCount < 0 ||
			OrderLine.ProductRow.DataTable == nullptr ||
			OrderLine.ProductRow.RowName.IsNone())
		{
			return false;
		}

		const FCSProductDataRow* ProductData =
			OrderLine.ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryPortalActor::SpawnOrderCrates"));
		if (ProductData == nullptr || ProductData->OrderPackQuantity < 1)
		{
			return false;
		}
	}

	UWorld* World = GetWorld();
	int32 SpawnIndex = 0;
	for (const FCSOrderLine& OrderLine : OrderLines)
	{
		if (OrderLine.CrateCount == 0)
		{
			continue;
		}

		const FCSProductDataRow* ProductData =
			OrderLine.ProductRow.GetRow<FCSProductDataRow>(TEXT("ACSDeliveryPortalActor::SpawnOrderCrates"));
		for (int32 CrateIndex = 0; CrateIndex < OrderLine.CrateCount; ++CrateIndex)
		{
			const int32 RowIndex = SpawnIndex / CratesPerRow;
			const int32 ColumnIndex = SpawnIndex % CratesPerRow;
			const FVector SpawnLocation =
				DeliverySpawnOrigin->GetComponentLocation() +
				DeliverySpawnOrigin->GetForwardVector() * (RowIndex * ForwardSpacing) +
				DeliverySpawnOrigin->GetRightVector() * (ColumnIndex * RightSpacing);
			const FTransform SpawnTransform(
				DeliverySpawnOrigin->GetComponentRotation(),
				SpawnLocation);

			ACSDeliveryCrateActor* SpawnedCrate =
				World->SpawnActorDeferred<ACSDeliveryCrateActor>(
					DeliveryCrateClass,
					SpawnTransform,
					this,
					nullptr,
					ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (SpawnedCrate == nullptr ||
				!SpawnedCrate->InitializeCrate(
					OrderLine.ProductRow,
					ProductData->OrderPackQuantity))
			{
				if (IsValid(SpawnedCrate))
				{
					SpawnedCrate->Destroy();
				}

				for (ACSDeliveryCrateActor* ExistingCrate : OutSpawnedCrates)
				{
					if (IsValid(ExistingCrate))
					{
						ExistingCrate->Destroy();
					}
				}
				OutSpawnedCrates.Reset();
				return false;
			}

			SpawnedCrate->FinishSpawning(SpawnTransform);
			if (!IsValid(SpawnedCrate))
			{
				for (ACSDeliveryCrateActor* ExistingCrate : OutSpawnedCrates)
				{
					if (IsValid(ExistingCrate))
					{
						ExistingCrate->Destroy();
					}
				}
				OutSpawnedCrates.Reset();
				return false;
			}

			OutSpawnedCrates.Add(SpawnedCrate);
			++SpawnIndex;
		}
	}

	return true;
}
