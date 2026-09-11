#include "Features/Employees/CSEmployeeWaitingArea.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"

ACSEmployeeWaitingArea::ACSEmployeeWaitingArea()
{
	PrimaryActorTick.bCanEverTick = false;
	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("WaitingFacing"));
	SetRootComponent(FacingArrow);
	FacingArrow->SetHiddenInGame(true);
	FacingArrow->SetCanEverAffectNavigation(false);
	FacingArrow->ArrowColor = FColor(255, 180, 60);
	FacingArrow->ArrowSize = 2.0f;

	WaitingAccessZone = CreateDefaultSubobject<UBoxComponent>(TEXT("WaitingAccessZone"));
	WaitingAccessZone->SetupAttachment(FacingArrow);
	WaitingAccessZone->ComponentTags.Add(TEXT("BuildPlacementAccessZone"));
	WaitingAccessZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaitingAccessZone->SetGenerateOverlapEvents(false);
	WaitingAccessZone->SetCanEverAffectNavigation(false);
	WaitingAccessZone->SetHiddenInGame(true);
	WaitingAccessZone->ShapeColor = FColor(255, 180, 60);
}

void ACSEmployeeWaitingArea::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const int32 Columns = FMath::Clamp(SlotsPerRow, 1, GetSlotCount());
	const int32 Rows = FMath::DivideAndRoundUp(GetSlotCount(), Columns);
	const float Spacing = FMath::Max(120.0f, SlotSpacing);
	WaitingAccessZone->SetRelativeLocation(FVector(-(Rows - 1) * Spacing * 0.5f, 0.0f, 100.0f));
	WaitingAccessZone->SetBoxExtent(FVector(Rows * Spacing * 0.5f, Columns * Spacing * 0.5f, 100.0f));
}

bool ACSEmployeeWaitingArea::GetSlotTransform(int32 SlotIndex, FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	if (SlotIndex < 0 || SlotIndex >= GetSlotCount())
	{
		return false;
	}
	const int32 Columns = FMath::Clamp(SlotsPerRow, 1, GetSlotCount());
	const float Spacing = FMath::Max(120.0f, SlotSpacing);
	const FVector LocalLocation(-(SlotIndex / Columns) * Spacing,
		((SlotIndex % Columns) - (Columns - 1) * 0.5f) * Spacing, 0.0f);
	OutTransform = FTransform(FRotator(0.0f, GetActorRotation().Yaw, 0.0f),
		GetActorTransform().TransformPosition(LocalLocation));
	return true;
}

bool ACSEmployeeWaitingArea::CanReserveSlot(const ACSStoreEmployeeCharacter* Employee, int32 SlotIndex) const
{
	FTransform CandidateTransform;
	if (!IsValid(Employee) || !GetSlotTransform(SlotIndex, CandidateTransform))
	{
		return false;
	}
	for (const auto& Reservation : Reservations)
	{
		const ACSStoreEmployeeCharacter* Occupant = Reservation.Value.Get();
		if (!IsValid(Occupant) || Occupant->IsActorBeingDestroyed() || Occupant == Employee)
		{
			continue;
		}
		FTransform OccupiedTransform;
		// 구역을 축소/스케일 조정하거나 큰 캡슐을 사용하는 BP에서도 몸이 겹치는 자리는 배정하지 않습니다.
		const float Clearance = Employee->GetCapsuleComponent()->GetScaledCapsuleRadius() +
			Occupant->GetCapsuleComponent()->GetScaledCapsuleRadius() + 40.0f;
		if (Reservation.Key == SlotIndex || (GetSlotTransform(Reservation.Key, OccupiedTransform) &&
			FVector::DistSquared2D(CandidateTransform.GetLocation(), OccupiedTransform.GetLocation()) < FMath::Square(Clearance)))
		{
			return false;
		}
	}
	return true;
}

bool ACSEmployeeWaitingArea::TryReserveSlot(ACSStoreEmployeeCharacter* Employee, int32 SlotIndex)
{
	if (!CanReserveSlot(Employee, SlotIndex))
	{
		return false;
	}
	ReleaseSlots(Employee);
	Reservations.Add(SlotIndex, Employee);
	return true;
}

bool ACSEmployeeWaitingArea::IsSlotReservedBy(const ACSStoreEmployeeCharacter* Employee, int32 SlotIndex) const
{
	const auto* Reservation = Reservations.Find(SlotIndex);
	return IsValid(Employee) && SlotIndex >= 0 && SlotIndex < GetSlotCount() &&
		Reservation != nullptr && Reservation->Get() == Employee;
}

void ACSEmployeeWaitingArea::ReleaseSlots(const ACSStoreEmployeeCharacter* Employee)
{
	for (auto It = Reservations.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid() || It.Value().Get() == Employee || It.Value()->IsActorBeingDestroyed())
		{
			It.RemoveCurrent();
		}
	}
}
