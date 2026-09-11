#include "Features/Employees/CSStoreEmployeeCharacter.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Features/Checkout/CSCheckoutCounterActor.h"
#include "Features/Employees/CSEmployeeWaitingArea.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Systems/Employees/CSStoreEmployeeSubsystem.h"

namespace
{
	constexpr float IdleAcceptanceRadius = 10.0f;
	constexpr float IdleSearchInterval = 2.0f;
	constexpr float IdleMoveTimeout = 30.0f;
}

bool ACSStoreEmployeeCharacter::TryProjectIdleLocation(const FVector& Location, FVector& OutNavigationLocation) const
{
	OutNavigationLocation = FVector::ZeroVector;
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	FNavLocation Projected;
	if (!NavSystem || !NavSystem->ProjectPointToNavigation(Location, Projected,
		FVector(1.0f, 1.0f, 300.0f), &GetNavAgentPropertiesRef()) ||
		FVector::DistSquared2D(Location, Projected.Location) > FMath::Square(1.0f))
	{
		// 다른 통로로 투영하면 계산대 반대편에 서거나 여러 대기 자리가 겹칠 수 있어 XY를 유지합니다.
		return false;
	}
	OutNavigationLocation = Projected.Location;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EmployeeIdleSpace), false, this);
	return !GetWorld()->OverlapBlockingTestByChannel(
		OutNavigationLocation + FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() + 2.0f),
		FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
		QueryParams);
}

bool ACSStoreEmployeeCharacter::ShouldWaitAtCheckoutCounter() const
{
	const TArray<ECSStoreEmployeeTaskType>& SupportedTasks = GetSupportedTaskTypes();
	return bWaitAtCheckoutCounter ||
		(SupportedTasks.Num() == 1 && SupportedTasks[0] == ECSStoreEmployeeTaskType::CheckoutService);
}

bool ACSStoreEmployeeCharacter::TryFindIdleDestination()
{
	UWorld* World = GetWorld();
	UCSStoreEmployeeSubsystem* Subsystem = World ? World->GetSubsystem<UCSStoreEmployeeSubsystem>() : nullptr;
	if (!Subsystem || IsCarryingCrate() || IsHoldingProduct())
	{
		return false;
	}

	struct FCandidate
	{
		AActor* Target;
		int32 Slot;
		FVector Location;
		FVector Anchor;
		double DistanceSquared;
		int32 Priority;
	};
	TArray<FCandidate> Candidates;
	ACSCheckoutCounterActor* ReservedCounter = Subsystem->GetReservedCheckoutStation(this);
	const bool bCheckoutIdle = ShouldWaitAtCheckoutCounter();
	if (bCheckoutIdle && CanHandleTask(ECSStoreEmployeeTaskType::CheckoutService))
	{
		for (TActorIterator<ACSCheckoutCounterActor> It(World); It; ++It)
		{
			FVector Location;
			if (Subsystem->CanUseCheckoutStation(this, *It) && It->TryGetCashierInteractionPoint(Location))
			{
				TArray<FVector> WaitingPoints;
				It->GetCashierWaitingPoints(WaitingPoints);
				for (const FVector& Point : WaitingPoints)
				{
					Candidates.Add({*It, INDEX_NONE, Point, Location, FVector::DistSquared2D(GetActorLocation(), Point),
						*It == ReservedCounter ? 0 : 1});
				}
			}
		}
	}
	// 계산원은 계산대가 예약되어 있거나 경로가 없어도 공용 대기 구역을 선택하지 않습니다.
	for (TActorIterator<ACSEmployeeWaitingArea> It(World); It && !bCheckoutIdle; ++It)
	{
		for (int32 Slot = 0; Slot < It->GetSlotCount(); ++Slot)
		{
			FTransform SlotTransform;
			if (It->CanReserveSlot(this, Slot) && It->GetSlotTransform(Slot, SlotTransform))
			{
				Candidates.Add({*It, Slot, SlotTransform.GetLocation(), SlotTransform.GetLocation(),
					FVector::DistSquared2D(GetActorLocation(), SlotTransform.GetLocation()), 2});
			}
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.Priority == B.Priority ? A.DistanceSquared < B.DistanceSquared : A.Priority < B.Priority;
	});
	int32 RejectedSpace = 0;
	int32 RejectedPath = 0;
	for (const FCandidate& Candidate : Candidates)
	{
		FVector NavigationLocation;
		if (!TryProjectIdleLocation(Candidate.Location, NavigationLocation))
		{
			++RejectedSpace;
			continue;
		}
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			World, GetNavAgentLocation(), NavigationLocation, this);
		if (!IsValid(Path) || !Path->IsValid() || Path->IsPartial())
		{
			++RejectedPath;
			continue;
		}
		bool bReserved = false;
		if (ACSEmployeeWaitingArea* Area = Cast<ACSEmployeeWaitingArea>(Candidate.Target))
		{
			bReserved = Area->TryReserveSlot(this, Candidate.Slot);
			if (bReserved)
			{
				Subsystem->ReleaseCheckoutStation(this);
			}
		}
		else if (ACSCheckoutCounterActor* Counter = Cast<ACSCheckoutCounterActor>(Candidate.Target))
		{
			bReserved = Subsystem->TryReserveCheckoutStation(this, Counter);
		}
		if (bReserved)
		{
			// 대기 목적지를 변경할 때 이전 자리와 이동을 반납합니다.
			if (ACSEmployeeWaitingArea* PreviousArea = Cast<ACSEmployeeWaitingArea>(IdleTarget.Get());
				PreviousArea && PreviousArea != Candidate.Target)
			{
				PreviousArea->ReleaseSlots(this);
			}
			if (bHasIdleDestination)
			{
				if (AAIController* AI = Cast<AAIController>(GetController()))
				{
					AI->StopMovement();
				}
				RestoreWorkRotationSettings();
			}
			IdleTarget = Candidate.Target;
			IdleSlotIndex = Candidate.Slot;
			IdleMoveGoal = NavigationLocation;
			IdleDestinationAnchor = Candidate.Anchor;
			IdleTargetTransform = Candidate.Target->GetActorTransform();
			UE_LOG(LogTemp, Log, TEXT("[EmployeeIdle] %s -> %s at %s"),
				*GetName(), *Candidate.Target->GetName(), *IdleMoveGoal.ToCompactString());
			bHasIdleDestination = true;
			bAtIdleDestination = false;
			bIdleMoveRequested = false;
			IdleMoveElapsedSeconds = 0.0f;
			return true;
		}
	}
	// 도달하지 못하는 계산대 예약으로 다른 직원의 계산 업무까지 막지 않습니다.
	Subsystem->ReleaseCheckoutStation(this);
	if (bCheckoutIdle)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmployeeIdle] %s: no reachable cashier position. Candidates=%d NavOrCollisionRejected=%d PathRejected=%d (zero candidates: check installation/role/reservations)"),
			*GetName(), Candidates.Num(), RejectedSpace, RejectedPath);
	}
	return false;
}

bool ACSStoreEmployeeCharacter::GetIdleDestination(FVector& OutLocation, FRotator& OutFacing) const
{
	OutLocation = FVector::ZeroVector;
	OutFacing = FRotator::ZeroRotator;
	if (const ACSEmployeeWaitingArea* Area = Cast<ACSEmployeeWaitingArea>(IdleTarget.Get()))
	{
		if (ShouldWaitAtCheckoutCounter())
		{
			// 이전 대기 예약이 남아 있어도 계산 전담 역할은 공용 구역으로 복귀하지 않습니다.
			return false;
		}
		FTransform SlotTransform;
		if (Area->IsActorBeingDestroyed() || !Area->IsSlotReservedBy(this, IdleSlotIndex) ||
			!Area->GetSlotTransform(IdleSlotIndex, SlotTransform))
		{
			return false;
		}
		OutLocation = SlotTransform.GetLocation();
		OutFacing = SlotTransform.Rotator();
		return true;
	}
	if (const ACSCheckoutCounterActor* Counter = Cast<ACSCheckoutCounterActor>(IdleTarget.Get()))
	{
		const UCSStoreEmployeeSubsystem* Subsystem = GetWorld()->GetSubsystem<UCSStoreEmployeeSubsystem>();
		if (!Subsystem || Subsystem->GetReservedCheckoutStation(this) != Counter ||
			!Counter->TryGetCashierInteractionPoint(OutLocation))
		{
			return false;
		}
		Counter->TryGetCashierInteractionFacing(GetActorLocation(), OutFacing);
		return true;
	}
	return false;
}

void ACSStoreEmployeeCharacter::UpdateIdleBehavior(float DeltaSeconds)
{
	IdleElapsedSeconds += DeltaSeconds;
	IdleSearchCountdown = FMath::Max(0.0f, IdleSearchCountdown - DeltaSeconds);
	if (IdleElapsedSeconds < IdleReturnDelaySeconds || IsCarryingCrate() || IsHoldingProduct())
	{
		return;
	}
	if (!bHasIdleDestination)
	{
		if (IdleSearchCountdown > 0.0f)
		{
			return;
		}
		IdleSearchCountdown = IdleSearchInterval;
		if (!TryFindIdleDestination())
		{
			// 구역 미배치/만석/경로 없음이면 현재 자리에서 다음 업무나 빈자리를 기다립니다.
			return;
		}
	}

	FVector Location;
	FRotator Facing;
	if (!GetIdleDestination(Location, Facing))
	{
		CancelIdleReturn();
		return;
	}
	// 계산대/구역을 편집 중에 이동했다면 목적지와 경로를 다시 검증합니다.
	if (!Location.Equals(IdleDestinationAnchor, 2.0f) ||
		!IdleTarget->GetActorTransform().Equals(IdleTargetTransform))
	{
		CancelIdleReturn();
		return;
	}
	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI)
	{
		CancelIdleReturn();
		return;
	}
	const bool bReached = FVector::DistSquared2D(GetActorLocation(), IdleMoveGoal) <= FMath::Square(IdleAcceptanceRadius + 2.0f) &&
		FMath::Abs(GetNavAgentLocation().Z - IdleMoveGoal.Z) < 100.0f;
	if (bReached)
	{
		if (!bAtIdleDestination)
		{
			AI->StopMovement();
			bAtIdleDestination = true;
			bIdleMoveRequested = false;
			IdleMoveElapsedSeconds = 0.0f;
		}
		UpdateFacingRotation(Facing, DeltaSeconds);
		return;
	}
	RestoreWorkRotationSettings();
	bAtIdleDestination = false;
	IdleMoveElapsedSeconds += DeltaSeconds;
	if (IdleMoveElapsedSeconds >= IdleMoveTimeout)
	{
		CancelIdleReturn();
		return;
	}
	if (bIdleMoveRequested)
	{
		if (AI->GetMoveStatus() != EPathFollowingStatus::Idle || IdleMoveElapsedSeconds < 1.0f)
		{
			return;
		}
		// 경로 실패 후 매 프레임 같은 MoveTo 요청을 반복하지 않습니다.
		CancelIdleReturn();
		return;
	}
	const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
		IdleMoveGoal, IdleAcceptanceRadius, false, true, false, true, nullptr, false);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmployeeIdle] %s: MoveTo failed at %s"),
			*GetName(), *IdleMoveGoal.ToCompactString());
		CancelIdleReturn();
		return;
	}
	bIdleMoveRequested = true;
}

void ACSStoreEmployeeCharacter::CancelIdleReturn(bool bReleaseCheckoutStation)
{
	if (ACSEmployeeWaitingArea* Area = Cast<ACSEmployeeWaitingArea>(IdleTarget.Get()))
	{
		Area->ReleaseSlots(this);
	}
	if (bReleaseCheckoutStation && GetWorld())
	{
		if (UCSStoreEmployeeSubsystem* Subsystem = GetWorld()->GetSubsystem<UCSStoreEmployeeSubsystem>())
		{
			Subsystem->ReleaseCheckoutStation(this);
		}
	}
	if (bHasIdleDestination)
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
	}
	RestoreWorkRotationSettings();
	IdleTarget.Reset();
	IdleSlotIndex = INDEX_NONE;
	bHasIdleDestination = false;
	bIdleMoveRequested = false;
	bAtIdleDestination = false;
	IdleMoveElapsedSeconds = 0.0f;
	IdleSearchCountdown = IdleSearchInterval;
}
