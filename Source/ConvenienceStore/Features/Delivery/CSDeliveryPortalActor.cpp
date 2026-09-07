#include "Features/Delivery/CSDeliveryPortalActor.h"

#include "Components/SceneComponent.h"
#include "Components/ArrowComponent.h"
#include "Data/CSProductTypes.h"
#include "Engine/World.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "NavigationSystem.h"
#include "Systems/Ordering/CSOrderSubsystem.h"

ACSDeliveryPortalActor::ACSDeliveryPortalActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DeliverySpawnOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("DeliverySpawnOrigin"));
	DeliverySpawnOrigin->SetupAttachment(SceneRoot);

	DeliveryApproachPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("DeliveryApproachPoint"));
	DeliveryApproachPoint->SetupAttachment(DeliverySpawnOrigin);
	// 기본값은 배송 상자 더미 뒤쪽입니다. 레벨에서 초록색 NavMesh 위의 빈 통로로 옮겨 사용하시면 됩니다.
	DeliveryApproachPoint->SetRelativeLocation(FVector(-220.0f, 180.0f, 0.0f));
	DeliveryApproachPoint->SetHiddenInGame(true);
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

bool ACSDeliveryPortalActor::TryGetEmployeeDeliveryApproachPoint(FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(DeliveryApproachPoint))
	{
		return false;
	}

	const UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World);
	FNavLocation ProjectedLocation;
	if (NavigationSystem == nullptr || !NavigationSystem->ProjectPointToNavigation(
		DeliveryApproachPoint->GetComponentLocation(),
		ProjectedLocation,
		FVector(120.0f, 120.0f, 300.0f)))
	{
		return false;
	}

	OutLocation = ProjectedLocation.Location;
	return true;
}

bool ACSDeliveryPortalActor::SpawnOrderCrates(
	const TArray<FCSOrderLine>& OrderLines,
	TArray<ACSDeliveryCrateActor*>& OutSpawnedCrates)
{
	// 기존 호출자는 이전과 동일하게 첫 번째 배송 위치부터 사용합니다.
	return SpawnOrderCrates(OrderLines, OutSpawnedCrates, 0);
}

bool ACSDeliveryPortalActor::SpawnOrderCrates(
	const TArray<FCSOrderLine>& OrderLines,
	TArray<ACSDeliveryCrateActor*>& OutSpawnedCrates,
	int32 StartingSpawnIndex)
{
	OutSpawnedCrates.Reset();
	if (!IsDeliveryConfigured() || StartingSpawnIndex < 0)
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
	int32 SpawnIndex = StartingSpawnIndex;
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

			// 실제 발주로 배송된 상자는 입고 대기로 표시해, 진열보다 창고 보관 업무를 먼저 만들도록 합니다.
			SpawnedCrate->SetAwaitingStockroomIntake(true);

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
