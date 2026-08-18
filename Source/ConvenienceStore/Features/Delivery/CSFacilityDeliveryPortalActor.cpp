#include "Features/Delivery/CSFacilityDeliveryPortalActor.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "Systems/Ordering/CSOrderSubsystem.h"

ACSFacilityDeliveryPortalActor::ACSFacilityDeliveryPortalActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FacilitySpawnOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("FacilitySpawnOrigin"));
	FacilitySpawnOrigin->SetupAttachment(SceneRoot);
}

void ACSFacilityDeliveryPortalActor::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UCSOrderSubsystem* OrderSubsystem = World->GetSubsystem<UCSOrderSubsystem>())
		{
			OrderSubsystem->RegisterFacilityDeliveryPortal(this);
		}
	}
}

void ACSFacilityDeliveryPortalActor::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCSOrderSubsystem* OrderSubsystem = World->GetSubsystem<UCSOrderSubsystem>())
		{
			OrderSubsystem->UnregisterFacilityDeliveryPortal(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool ACSFacilityDeliveryPortalActor::IsDeliveryConfigured() const
{
	return IsValid(FacilitySpawnOrigin) &&
		GetWorld() != nullptr &&
		FacilitiesPerRow >= 1 &&
		!FacilityActorClasses.IsEmpty();
}

ECFacilityOrderFailureReason ACSFacilityDeliveryPortalActor::ValidateOrderFacilities(
	const TArray<FCSFacilityOrderLine>& OrderLines) const
{
	if (!IsDeliveryConfigured())
	{
		return ECFacilityOrderFailureReason::DeliveryUnavailable;
	}

	for (const FCSFacilityOrderLine& OrderLine : OrderLines)
	{
		if (OrderLine.Quantity == 0)
		{
			continue;
		}

		const TSubclassOf<AActor>* FacilityActorClass =
			FacilityActorClasses.Find(OrderLine.FacilityRow.RowName);
		UClass* FacilityClass = FacilityActorClass != nullptr
			? FacilityActorClass->Get()
			: nullptr;
		if (!IsValid(FacilityClass) ||
			!FacilityClass->ImplementsInterface(UCSBuildableFacility::StaticClass()))
		{
			return ECFacilityOrderFailureReason::FacilityClassUnavailable;
		}
	}

	return ECFacilityOrderFailureReason::None;
}

ECFacilityOrderFailureReason ACSFacilityDeliveryPortalActor::SpawnOrderFacilities(
	const TArray<FCSFacilityOrderLine>& OrderLines,
	TArray<AActor*>& OutSpawnedFacilities)
{
	// 기존 호출자는 이전과 동일하게 첫 번째 배송 위치부터 사용합니다.
	return SpawnOrderFacilities(OrderLines, OutSpawnedFacilities, 0);
}

ECFacilityOrderFailureReason ACSFacilityDeliveryPortalActor::SpawnOrderFacilities(
	const TArray<FCSFacilityOrderLine>& OrderLines,
	TArray<AActor*>& OutSpawnedFacilities,
	int32 StartingSpawnIndex)
{
	OutSpawnedFacilities.Reset();
	if (StartingSpawnIndex < 0)
	{
		return ECFacilityOrderFailureReason::FacilitySpawnFailed;
	}

	const ECFacilityOrderFailureReason ValidationFailureReason =
		ValidateOrderFacilities(OrderLines);
	if (ValidationFailureReason != ECFacilityOrderFailureReason::None)
	{
		return ValidationFailureReason;
	}

	auto DestroySpawnedFacilities = [&OutSpawnedFacilities]()
	{
		for (AActor* SpawnedFacility : OutSpawnedFacilities)
		{
			if (IsValid(SpawnedFacility))
			{
				SpawnedFacility->Destroy();
			}
		}
		OutSpawnedFacilities.Reset();
	};

	UWorld* World = GetWorld();
	int32 SpawnIndex = StartingSpawnIndex;
	for (const FCSFacilityOrderLine& OrderLine : OrderLines)
	{
		if (OrderLine.Quantity == 0)
		{
			continue;
		}

		const TSubclassOf<AActor> FacilityActorClass =
			FacilityActorClasses.FindChecked(OrderLine.FacilityRow.RowName);
		for (int32 FacilityIndex = 0;
			FacilityIndex < OrderLine.Quantity;
			++FacilityIndex)
		{
			const int32 RowIndex = SpawnIndex / FacilitiesPerRow;
			const int32 ColumnIndex = SpawnIndex % FacilitiesPerRow;
			const FVector SpawnLocation =
				FacilitySpawnOrigin->GetComponentLocation() +
				FacilitySpawnOrigin->GetForwardVector() * (RowIndex * ForwardSpacing) +
				FacilitySpawnOrigin->GetRightVector() * (ColumnIndex * RightSpacing);
			const FTransform SpawnTransform(
				FacilitySpawnOrigin->GetComponentRotation(),
				SpawnLocation);

			AActor* SpawnedFacility = World->SpawnActorDeferred<AActor>(
				FacilityActorClass,
				SpawnTransform,
				this,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (!IsValid(SpawnedFacility))
			{
				DestroySpawnedFacilities();
				return ECFacilityOrderFailureReason::FacilitySpawnFailed;
			}

			SpawnedFacility->FinishSpawning(SpawnTransform);
			if (!IsValid(SpawnedFacility))
			{
				DestroySpawnedFacilities();
				return ECFacilityOrderFailureReason::SpawnedFacilityInvalid;
			}

			OutSpawnedFacilities.Add(SpawnedFacility);
			if (!SpawnedFacility->Implements<UCSBuildableFacility>())
			{
				DestroySpawnedFacilities();
				return ECFacilityOrderFailureReason::SpawnedFacilityInvalid;
			}

			const FName SpawnedFacilityId =
				ICSBuildableFacility::Execute_GetFacilityId(SpawnedFacility);
			if (SpawnedFacilityId != OrderLine.FacilityRow.RowName)
			{
				DestroySpawnedFacilities();
				return ECFacilityOrderFailureReason::FacilityIdMismatch;
			}

			ICSBuildableFacility::Execute_InitializeAsUninstalledFacility(
				SpawnedFacility);
			if (!IsValid(SpawnedFacility))
			{
				DestroySpawnedFacilities();
				return ECFacilityOrderFailureReason::SpawnedFacilityInvalid;
			}
			if (ICSBuildableFacility::Execute_IsFacilityInstalled(SpawnedFacility))
			{
				DestroySpawnedFacilities();
				return ECFacilityOrderFailureReason::FacilityInitializationFailed;
			}

			++SpawnIndex;
		}
	}

	return ECFacilityOrderFailureReason::None;
}
