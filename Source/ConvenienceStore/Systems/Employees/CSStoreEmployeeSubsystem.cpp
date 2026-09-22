#include "Systems/Employees/CSStoreEmployeeSubsystem.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Features/Checkout/CSCheckoutCounterActor.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Storage/CSFreshnessSafeZoneActor.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"

namespace
{
bool IsEmployeeManageableProduct(const ACSProductPickupActor* Product)
{
	// 배치 프리뷰와 계산대 표시품은 실제 재고와 같은 부모 클래스를 쓰지만,
	// 표시 전용 액터이므로 직원 업무 대상이 될 수 없습니다.
	return IsValid(Product) && !Product->IsA<ACSProductPlacementPreviewActor>() &&
		!Product->IsExcludedFromStockCount();
}

bool IsActorHeldByPlayer(const UWorld* World, const AActor* Actor)
{
	if (!IsValid(World) || !IsValid(Actor))
	{
		return false;
	}

	for (TActorIterator<ACSPlayerCharacter> PlayerIt(World); PlayerIt; ++PlayerIt)
	{
		// 부착 관계가 갱신되기 전의 짧은 프레임도 포함해, 플레이어 소유 물건은 직원 업무에서 항상 제외합니다.
		if (IsValid(*PlayerIt) && (*PlayerIt)->GetHeldActor() == Actor)
		{
			return true;
		}
	}

	return false;
}

bool IsProductRowCompatibleWithEmptyShelf(
	const FDataTableRowHandle& ProductRow,
	const ACSShelfActor* Shelf)
{
	if (!IsValid(Shelf))
	{
		return false;
	}

	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("UCSStoreEmployeeSubsystem::IsProductRowCompatibleWithEmptyShelf"));
	if (ProductData == nullptr)
	{
		return false;
	}

	const ECSProductShelfType ShelfType = Shelf->GetShelfStorageType();
	if (ShelfType == ECSProductShelfType::UtilityShelf)
	{
		// 일반 진열대는 상온 상품만 받도록 하여 냉장·냉동 상품의 신선도가 잘못 관리되지 않게 합니다.
		return ProductData->ShelfType != ECSProductShelfType::ChilledShelf &&
			ProductData->ShelfType != ECSProductShelfType::FreezerShelf;
	}

	// 냉장·냉동 및 특수 진열대는 상품 데이터에 지정된 보관 타입이 정확히 같은 경우에만 채웁니다.
	return ProductData->ShelfType == ShelfType;
}

bool IsProductCompatibleWithEmptyShelf(
	const ACSDeliveryCrateActor* Crate,
	const ACSShelfActor* Shelf)
{
	return IsValid(Crate) &&
		IsProductRowCompatibleWithEmptyShelf(Crate->GetProductRow(), Shelf);
}

bool CanCrateRestockShelf(
	const ACSDeliveryCrateActor* Crate,
	const ACSShelfActor* Shelf,
	bool bAllowCrossTierDuplicateFallback = false)
{
	if (!IsValid(Crate) || !IsValid(Shelf) || !Shelf->CanEmployeeRestock() ||
		Shelf->GetEmptySlotCount() <= 0)
	{
		return false;
	}

	return Shelf->CanEmployeeRestockProduct(
		Crate->GetProductRow(), bAllowCrossTierDuplicateFallback);
}

bool CanLooseProductRestockShelf(
	const ACSProductPickupActor* Product,
	const ACSShelfActor* Shelf,
	bool bAllowCrossTierDuplicateFallback = false)
{
	if (!IsEmployeeManageableProduct(Product) || !IsValid(Shelf) || !Shelf->CanEmployeeRestock() ||
		Shelf->GetEmptySlotCount() <= 0)
	{
		return false;
	}

	return Shelf->CanEmployeeRestockProduct(
		Product->GetProductRow(), bAllowCrossTierDuplicateFallback);
}

bool IsPerishableProductRow(const FDataTableRowHandle& ProductRow)
{
	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("UCSStoreEmployeeSubsystem::IsPerishableProductRow"));
	return ProductData != nullptr &&
		(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
			ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
}

bool IsSafePerishableParent(const AActor* ParentActor, ECSProductShelfType ProductStorageType)
{
	if (const ACSShelfActor* Shelf = Cast<ACSShelfActor>(ParentActor))
	{
		return Shelf->GetShelfStorageType() == ProductStorageType;
	}

	if (const ACSFreshnessSafeZoneActor* Storage = Cast<ACSFreshnessSafeZoneActor>(ParentActor))
	{
		return Storage->GetStorageType() == ProductStorageType;
	}

	return false;
}

bool IsPerishableProductSafelyStored(const ACSProductPickupActor* Product)
{
	if (!IsValid(Product))
	{
		return false;
	}

	const FCSProductDataRow* ProductData = Product->GetProductRow().GetRow<FCSProductDataRow>(
		TEXT("UCSStoreEmployeeSubsystem::IsPerishableProductSafelyStored"));
	return ProductData != nullptr &&
		IsSafePerishableParent(Product->GetAttachParentActor(), ProductData->ShelfType);
}

bool IsPerishableCrateSafelyStored(const ACSDeliveryCrateActor* Crate)
{
	if (!IsValid(Crate))
	{
		return false;
	}

	const FCSProductDataRow* ProductData = Crate->GetProductRow().GetRow<FCSProductDataRow>(
		TEXT("UCSStoreEmployeeSubsystem::IsPerishableCrateSafelyStored"));
	return ProductData != nullptr &&
		IsSafePerishableParent(Crate->GetAttachParentActor(), ProductData->ShelfType);
}

bool IsPerishableProtectionTaskAlreadySafe(const FCSStoreEmployeeTask& Task)
{
	if (Task.Type != ECSStoreEmployeeTaskType::ProtectPerishableStock)
	{
		return false;
	}

	if (const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Task.SourceActor))
	{
		return IsPerishableProductSafelyStored(Product);
	}

	if (const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Task.SourceActor))
	{
		return IsPerishableCrateSafelyStored(Crate);
	}

	return false;
}

bool IsStoreCrateTaskAlreadyStored(const FCSStoreEmployeeTask& Task)
{
	const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Task.SourceActor);
	return Task.Type == ECSStoreEmployeeTaskType::StoreCrate && IsValid(Crate) &&
		Cast<ACSFreshnessSafeZoneActor>(Crate->GetAttachParentActor()) != nullptr;
}

bool CanReserveTaskSource(
	const FCSStoreEmployeeTask& Task,
	const ACSStoreEmployeeCharacter* Employee)
{
	if (const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Task.SourceActor))
	{
		return Crate->CanBeReservedByEmployee(Employee, Task.TaskId);
	}

	if (const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Task.SourceActor))
	{
		return Product->CanBeReservedByEmployee(Employee, Task.TaskId);
	}

	// 계산 업무처럼 운반할 물건이 없는 업무는 액터 예약이 필요 없습니다.
	return true;
}

bool ReserveTaskSource(FCSStoreEmployeeTask& Task, ACSStoreEmployeeCharacter* Employee)
{
	if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Task.SourceActor))
	{
		return Crate->TryReserveForEmployee(Employee, Task.TaskId);
	}

	if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Task.SourceActor))
	{
		return Product->TryReserveForEmployee(Employee, Task.TaskId);
	}

	return true;
}

void ReleaseTaskSourceReservation(
	const FCSStoreEmployeeTask& Task,
	const ACSStoreEmployeeCharacter* Employee)
{
	if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Task.SourceActor))
	{
		Crate->ReleaseEmployeeReservation(Employee, Task.TaskId);
	}
	else if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Task.SourceActor))
	{
		Product->ReleaseEmployeeReservation(Employee, Task.TaskId);
	}
}
}

void UCSStoreEmployeeSubsystem::Tick(float DeltaTime)
{
	RefreshElapsedSeconds += DeltaTime;
	if (RefreshElapsedSeconds < RefreshIntervalSeconds)
	{
		return;
	}

	// 긴 프레임이 발생해도 다음 검사 주기가 누적 폭주하지 않도록 경과 시간을 초기화합니다.
	RefreshElapsedSeconds = 0.0f;
	RefreshTasks();
}

void UCSStoreEmployeeSubsystem::RefreshTasksImmediately()
{
	// 설정을 막 바꾼 상품도 즉시 오배치 업무 후보가 되도록, 다음 1초 검사 주기를 기다리지 않습니다.
	RefreshElapsedSeconds = 0.0f;
	RefreshTasks();
}

TStatId UCSStoreEmployeeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCSStoreEmployeeSubsystem, STATGROUP_Tickables);
}

void UCSStoreEmployeeSubsystem::RegisterEmployee(ACSStoreEmployeeCharacter* Employee)
{
	if (IsValid(Employee))
	{
		// 직원끼리만 상호 이동 충돌을 무시해, 좁은 통로에서 서로 밀리거나 멈추지 않게 합니다.
		for (ACSStoreEmployeeCharacter* ExistingEmployee : RegisteredEmployees)
		{
			if (IsValid(ExistingEmployee) && ExistingEmployee != Employee)
			{
				Employee->SetIgnoreEmployeeMovement(ExistingEmployee, true);
				ExistingEmployee->SetIgnoreEmployeeMovement(Employee, true);
			}
		}
		RegisteredEmployees.AddUnique(Employee);
	}
}

void UCSStoreEmployeeSubsystem::UnregisterEmployee(ACSStoreEmployeeCharacter* Employee)
{
	ReleaseCheckoutStation(Employee);
	RegisteredEmployees.Remove(Employee);
	for (ACSStoreEmployeeCharacter* ExistingEmployee : RegisteredEmployees)
	{
		if (IsValid(ExistingEmployee) && ExistingEmployee != Employee)
		{
			ExistingEmployee->SetIgnoreEmployeeMovement(Employee, false);
		}
	}
	for (FCSStoreEmployeeTask& Task : Tasks)
	{
		// 제거되는 직원이 맡던 업무는 다른 직원이 이어서 처리할 수 있게 대기 상태로 되돌립니다.
		if (Task.AssignedEmployee == Employee &&
			Task.Status == ECSStoreEmployeeTaskStatus::InProgress)
		{
			ReleaseTaskSourceReservation(Task, Employee);
			Task.AssignedEmployee = nullptr;
			Task.Status = ECSStoreEmployeeTaskStatus::Pending;
		}
	}
}

bool UCSStoreEmployeeSubsystem::CanUseCheckoutStation(
	const ACSStoreEmployeeCharacter* Employee, const ACSCheckoutCounterActor* Counter) const
{
	FVector UnusedLocation;
	if (!IsValid(Employee) || !IsValid(Counter) || !Counter->TryGetCashierInteractionPoint(UnusedLocation))
	{
		return false;
	}
	const auto* Reservation = CheckoutStations.Find(
		TWeakObjectPtr<ACSCheckoutCounterActor>(const_cast<ACSCheckoutCounterActor*>(Counter)));
	if (Reservation != nullptr && Reservation->IsValid() &&
		!Reservation->Get()->IsActorBeingDestroyed() && Reservation->Get() != Employee)
	{
		return false;
	}
	return !Tasks.ContainsByPredicate([Employee, Counter](const FCSStoreEmployeeTask& Task)
	{
		return Task.Type == ECSStoreEmployeeTaskType::CheckoutService && Task.TargetActor == Counter &&
			Task.Status == ECSStoreEmployeeTaskStatus::InProgress && IsValid(Task.AssignedEmployee) &&
			Task.AssignedEmployee != Employee;
	});
}

bool UCSStoreEmployeeSubsystem::TryReserveCheckoutStation(
	ACSStoreEmployeeCharacter* Employee, ACSCheckoutCounterActor* Counter)
{
	if (!CanUseCheckoutStation(Employee, Counter))
	{
		return false;
	}
	ReleaseCheckoutStation(Employee);
	CheckoutStations.Add(Counter, Employee);
	return true;
}

ACSCheckoutCounterActor* UCSStoreEmployeeSubsystem::GetReservedCheckoutStation(const ACSStoreEmployeeCharacter* Employee) const
{
	for (const auto& Reservation : CheckoutStations)
	{
		if (Reservation.Key.IsValid() && Reservation.Value.Get() == Employee)
		{
			return Reservation.Key.Get();
		}
	}
	return nullptr;
}

void UCSStoreEmployeeSubsystem::ReleaseCheckoutStation(const ACSStoreEmployeeCharacter* Employee)
{
	for (auto It = CheckoutStations.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid() || It.Value().Get() == Employee || It.Value()->IsActorBeingDestroyed())
		{
			It.RemoveCurrent();
		}
	}
}

bool UCSStoreEmployeeSubsystem::TryClaimBestTask(
	ACSStoreEmployeeCharacter* Employee,
	const TArray<ECSStoreEmployeeTaskType>& SupportedTaskTypes,
	FCSStoreEmployeeTask& OutTask,
	FString* OutNoClaimReason)
{
	if (!IsValid(Employee) || !Employee->CanWork())
	{
		if (OutNoClaimReason != nullptr)
		{
			*OutNoClaimReason = TEXT("직원 참조가 유효하지 않습니다.");
		}
		return false;
	}

	FCSStoreEmployeeTask* BestTask = nullptr;
	int32 PendingTaskCount = 0;
	int32 InProgressOrClosedTaskCount = 0;
	int32 InvalidTargetTaskCount = 0;
	int32 AlreadySafeTaskCount = 0;
	int32 PlayerHeldSourceTaskCount = 0;
	int32 UnsupportedTaskCount = 0;
	int32 ReservedByOtherEmployeeTaskCount = 0;
	int32 DeferredTaskCount = 0;
	for (FCSStoreEmployeeTask& Task : Tasks)
	{
		if (Task.Status != ECSStoreEmployeeTaskStatus::Pending)
		{
			++InProgressOrClosedTaskCount;
			continue;
		}

		++PendingTaskCount;
		// 입고 시점에 예약된 진열대가 비었을 수 있으므로 수령 직전에 다시 판단합니다.
		const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(Task.SourceActor);
		if (SupportedTaskTypes.Contains(ECSStoreEmployeeTaskType::RestockShelf) && IsValid(Crate) &&
			IsPerishableProductRow(Crate->GetProductRow()) && !IsActorHeldByPlayer(GetWorld(), Crate) &&
			(Task.Type == ECSStoreEmployeeTaskType::StoreCrate ||
				(Task.Type == ECSStoreEmployeeTaskType::ProtectPerishableStock &&
					Cast<ACSFreshnessSafeZoneActor>(Task.TargetActor) != nullptr)))
		{
			ACSShelfActor* BestShelf = nullptr;
			float BestDistance = TNumericLimits<float>::Max();
			for (TActorIterator<ACSShelfActor> It(GetWorld()); It; ++It)
			{
				ACSShelfActor* Shelf = *It;
				if (!CanCrateRestockShelf(Crate, Shelf, true) || HasPendingReorganizationFromShelf(Shelf) ||
					HasActiveTask(ECSStoreEmployeeTaskType::RestockShelf, Shelf)) continue;
				const float Distance = FVector::DistSquared(Crate->GetActorLocation(), Shelf->GetActorLocation());
				if (Distance < BestDistance) { BestDistance = Distance; BestShelf = Shelf; }
			}
			if (IsValid(BestShelf))
			{
				Task.Type = ECSStoreEmployeeTaskType::RestockShelf;
				Task.TargetActor = BestShelf;
				Task.ProductRow = Crate->GetProductRow();
				Task.bAllowCrossTierDuplicateFallback = true;
				Task.RequestedQuantity = FMath::Min(Crate->GetRemainingQuantity(),
					BestShelf->GetEmployeeRestockableSlotCount(Task.ProductRow, true));
			}
		}
		if (!IsValid(Task.TargetActor) ||
			(Task.Type != ECSStoreEmployeeTaskType::CheckoutService && !IsValid(Task.SourceActor)))
		{
			++InvalidTargetTaskCount;
			continue;
		}

		if (IsPerishableProtectionTaskAlreadySafe(Task) || IsStoreCrateTaskAlreadyStored(Task))
		{
			++AlreadySafeTaskCount;
			continue;
		}

		if (IsActorHeldByPlayer(GetWorld(), Task.SourceActor))
		{
			++PlayerHeldSourceTaskCount;
			continue;
		}

		if (!SupportedTaskTypes.Contains(Task.Type))
		{
			++UnsupportedTaskCount;
			continue;
		}

		if (IsTaskDeferred(Task))
		{
			++DeferredTaskCount;
			continue;
		}

		if (!CanReserveTaskSource(Task, Employee))
		{
			++ReservedByOtherEmployeeTaskCount;
			continue;
		}

		if (Task.Type == ECSStoreEmployeeTaskType::CheckoutService &&
			!CanUseCheckoutStation(Employee, Cast<ACSCheckoutCounterActor>(Task.TargetActor)))
		{
			++ReservedByOtherEmployeeTaskCount;
			continue;
		}

		if (BestTask == nullptr || Task.Priority > BestTask->Priority)
		{
			BestTask = &Task;
		}
	}

	if (BestTask == nullptr)
	{
		if (OutNoClaimReason != nullptr)
		{
			if (Tasks.IsEmpty())
			{
				*OutNoClaimReason = TEXT("생성된 업무가 없습니다. 입고 상자, 빈 진열대, 오배치 상품, 계산 대기열을 확인하세요.");
			}
			else
			{
				*OutNoClaimReason = FString::Printf(
					TEXT("전체=%d, 대기=%d, 진행/종료=%d, 대상 무효=%d, 이미 안전 보관=%d, 플레이어 소지=%d, 역할 미지원=%d, 다른 직원 예약=%d, 실패 보류=%d"),
					Tasks.Num(),
					PendingTaskCount,
					InProgressOrClosedTaskCount,
					InvalidTargetTaskCount,
					AlreadySafeTaskCount,
					PlayerHeldSourceTaskCount,
					UnsupportedTaskCount,
					ReservedByOtherEmployeeTaskCount,
					DeferredTaskCount);
			}
		}
		return false;
	}

	if (!ReserveTaskSource(*BestTask, Employee))
	{
		if (OutNoClaimReason != nullptr)
		{
			*OutNoClaimReason = TEXT("선택한 업무의 상자 또는 상품이 다른 직원에게 예약되었습니다.");
		}
		return false;
	}

	if (BestTask->Type == ECSStoreEmployeeTaskType::CheckoutService &&
		!TryReserveCheckoutStation(Employee, Cast<ACSCheckoutCounterActor>(BestTask->TargetActor)))
	{
		ReleaseTaskSourceReservation(*BestTask, Employee);
		return false;
	}
	BestTask->AssignedEmployee = Employee;
	BestTask->Status = ECSStoreEmployeeTaskStatus::InProgress;
	OutTask = *BestTask;
	return true;
}

bool UCSStoreEmployeeSubsystem::ReplaceAssignedTaskSource(int32 TaskId, ACSStoreEmployeeCharacter* Employee, AActor* NewSource, int32 Quantity)
{
	ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(NewSource);
	if (!IsValid(Crate) || !IsTaskAssignedToEmployee(TaskId, Employee)) return false;
	FCSStoreEmployeeTask* Task = Tasks.FindByPredicate([TaskId](const FCSStoreEmployeeTask& Value) { return Value.TaskId == TaskId; });
	if (!Task || !Crate->TryReserveForEmployee(Employee, TaskId)) return false;
	ReleaseTaskSourceReservation(*Task, Employee);
	Task->SourceActor = Crate;
	Task->RequestedQuantity = Quantity;
	return true;
}

bool UCSStoreEmployeeSubsystem::CancelTaskForPlayerPickup(AActor* SourceActor)
{
	if (!IsValid(SourceActor))
	{
		return false;
	}

	TArray<TWeakObjectPtr<ACSStoreEmployeeCharacter>> AffectedEmployees;
	if (ACSStoreEmployeeCharacter* Carrier = Cast<ACSStoreEmployeeCharacter>(SourceActor->GetAttachParentActor()))
		AffectedEmployees.AddUnique(Carrier);
	for (FCSStoreEmployeeTask& Task : Tasks)
	{
		if (Task.SourceActor != SourceActor ||
			(Task.Status != ECSStoreEmployeeTaskStatus::Pending &&
				Task.Status != ECSStoreEmployeeTaskStatus::InProgress))
		{
			continue;
		}

		if (IsValid(Task.AssignedEmployee)) AffectedEmployees.AddUnique(Task.AssignedEmployee);
		ReleaseTaskSourceReservation(Task, Task.AssignedEmployee);
		Task.AssignedEmployee = nullptr;
		Task.Status = ECSStoreEmployeeTaskStatus::Cancelled;
		UE_LOG(LogTemp, Log,
			TEXT("[StoreEmployee] 플레이어가 %s을(를) 집어 %d번 직원 업무를 취소했습니다."),
			*SourceActor->GetName(),
			Task.TaskId);
	}
	// 콜백이 업무 배열을 변경할 수 있으므로 순회를 마친 뒤 직원 상태를 정리합니다.
	for (const TWeakObjectPtr<ACSStoreEmployeeCharacter>& Employee : AffectedEmployees)
		if (Employee.IsValid()) Employee->RelinquishInventoryToPlayer(SourceActor);
	// 이미 사라진 업무의 예약도 플레이어 집기를 막지 않도록 남은 예약을 해제합니다.
	if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(SourceActor)) Product->ClearEmployeeReservationForPlayer();
	if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(SourceActor)) Crate->ClearEmployeeReservationForPlayer();
	return true;
}

bool UCSStoreEmployeeSubsystem::IsTaskAssignedToEmployee(
	int32 TaskId,
	const ACSStoreEmployeeCharacter* Employee) const
{
	return IsValid(Employee) && Tasks.ContainsByPredicate([TaskId, Employee](const FCSStoreEmployeeTask& Task)
	{
		return Task.TaskId == TaskId && Task.AssignedEmployee == Employee &&
			Task.Status == ECSStoreEmployeeTaskStatus::InProgress;
	});
}

void UCSStoreEmployeeSubsystem::FinishTask(
	int32 TaskId,
	ACSStoreEmployeeCharacter* Employee,
	bool bSucceeded)
{
	for (FCSStoreEmployeeTask& Task : Tasks)
	{
		if (Task.TaskId != TaskId || Task.AssignedEmployee != Employee)
		{
			continue;
		}

		Task.Status = bSucceeded
			? ECSStoreEmployeeTaskStatus::Completed
			: ECSStoreEmployeeTaskStatus::Failed;
		if (bSucceeded)
		{
			ResetTaskFailure(Task);
		}
		else
		{
			RegisterTaskFailure(Task);
			if (Task.Type == ECSStoreEmployeeTaskType::CheckoutService)
			{
				ReleaseCheckoutStation(Employee);
			}
		}
		ReleaseTaskSourceReservation(Task, Employee);
		Task.AssignedEmployee = nullptr;
		return;
	}
}

void UCSStoreEmployeeSubsystem::RefreshTasks()
{
	for (auto RetryStateIt = TaskRetryStates.CreateIterator(); RetryStateIt; ++RetryStateIt)
	{
		if (!RetryStateIt.Key().IsValid())
		{
			RetryStateIt.RemoveCurrent();
		}
	}

	// 이미 끝난 업무와 대상이 제거된 업무는 관리 배열에서 제거해 장시간 플레이에도 참조가 쌓이지 않게 합니다.
	Tasks.RemoveAll([](const FCSStoreEmployeeTask& Task)
	{
		const ACSProductPickupActor* SourceProduct = Cast<ACSProductPickupActor>(Task.SourceActor);
		const bool bRemoveTask = !IsValid(Task.TargetActor) ||
			(Task.Status == ECSStoreEmployeeTaskStatus::Pending &&
				Task.Type != ECSStoreEmployeeTaskType::CheckoutService && !IsValid(Task.SourceActor)) ||
			(Task.Status == ECSStoreEmployeeTaskStatus::Pending && IsStoreCrateTaskAlreadyStored(Task)) ||
			(IsValid(SourceProduct) && !IsEmployeeManageableProduct(SourceProduct)) ||
			IsPerishableProtectionTaskAlreadySafe(Task) ||
			Task.Status == ECSStoreEmployeeTaskStatus::Completed ||
			Task.Status == ECSStoreEmployeeTaskStatus::Failed ||
			Task.Status == ECSStoreEmployeeTaskStatus::Cancelled;
		if (bRemoveTask)
		{
			// 삭제 뒤에는 FinishTask가 업무를 찾지 못하므로 원본 예약을 먼저 해제합니다.
			ReleaseTaskSourceReservation(Task, Task.AssignedEmployee);
		}
		return bRemoveTask;
	});
	RegisteredEmployees.RemoveAll([](const ACSStoreEmployeeCharacter* Employee)
	{
		return !IsValid(Employee);
	});

	RefreshCheckoutTasks();
	RefreshPerishableCrateRestockTasks();
	RefreshInboundIntakeTasks();
	RefreshPerishableProtectionTasks();
	RefreshReorganizationTasks();
	RefreshRestockTasks();
	RefreshStorageTasks();
}

AActor* UCSStoreEmployeeSubsystem::GetTaskRetryKeyActor(const FCSStoreEmployeeTask& Task) const
{
	// 운반 업무는 상자/낱개 상품을 키로 삼고, 원본이 없는 계산 업무는 대상 액터를 사용합니다.
	return IsValid(Task.SourceActor) ? Task.SourceActor.Get() : Task.TargetActor.Get();
}

bool UCSStoreEmployeeSubsystem::IsTaskDeferred(const FCSStoreEmployeeTask& Task) const
{
	const AActor* RetryKeyActor = GetTaskRetryKeyActor(Task);
	if (!IsValid(RetryKeyActor) || !IsValid(GetWorld()))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> RetryKey(const_cast<AActor*>(RetryKeyActor));
	const FTaskRetryState* RetryState = TaskRetryStates.Find(RetryKey);
	return RetryState != nullptr && RetryState->DeferredUntilSeconds > GetWorld()->GetTimeSeconds();
}

void UCSStoreEmployeeSubsystem::RegisterTaskFailure(const FCSStoreEmployeeTask& Task)
{
	AActor* RetryKeyActor = GetTaskRetryKeyActor(Task);
	UWorld* World = GetWorld();
	if (!IsValid(RetryKeyActor) || !IsValid(World))
	{
		return;
	}

	const TWeakObjectPtr<AActor> RetryKey(RetryKeyActor);
	FTaskRetryState& RetryState = TaskRetryStates.FindOrAdd(RetryKey);
	++RetryState.ConsecutiveFailureCount;
	const bool bEnterCooldown = RetryState.ConsecutiveFailureCount >= FailureCountBeforeCooldown;
	const float DeferredSeconds = bEnterCooldown ? FailureCooldownSeconds : RetryDelaySeconds;
	RetryState.DeferredUntilSeconds = World->GetTimeSeconds() + DeferredSeconds;

	UE_LOG(LogTemp, Warning,
		TEXT("[StoreEmployee] %d번 업무 대상 %s 처리 실패 %d/%d: %.1f초 동안 보류%s"),
		Task.TaskId,
		*RetryKeyActor->GetName(),
		RetryState.ConsecutiveFailureCount,
		FailureCountBeforeCooldown,
		DeferredSeconds,
		bEnterCooldown ? TEXT(" 후 다른 업무를 우선 수행합니다.") : TEXT(" 후 재시도합니다."));

	if (bEnterCooldown)
	{
		// 긴 보류가 끝난 뒤에는 다시 세 번의 시도를 허용합니다.
		RetryState.ConsecutiveFailureCount = 0;
	}
}

void UCSStoreEmployeeSubsystem::ResetTaskFailure(const FCSStoreEmployeeTask& Task)
{
	if (AActor* RetryKeyActor = GetTaskRetryKeyActor(Task))
	{
		TaskRetryStates.Remove(TWeakObjectPtr<AActor>(RetryKeyActor));
	}
}

void UCSStoreEmployeeSubsystem::RefreshCheckoutTasks()
{
	UWorld* World = GetWorld();
	UCSCustomerRuntimeSubsystem* CustomerSubsystem =
		IsValid(World) ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr;
	// 손님이 0번 자리에 도착하기 전에는 계산 업무를 만들지 않습니다.
	// 계산대의 CanEmployeeProcessCheckout도 같은 게이트를 보므로 여기서 미리 걸러 순회를 아낍니다.
	if (!IsValid(CustomerSubsystem) || !IsValid(CustomerSubsystem->GetCheckoutReadyCustomer()))
	{
		return;
	}

	for (TActorIterator<ACSCheckoutCounterActor> It(World); It; ++It)
	{
		ACSCheckoutCounterActor* Counter = *It;
		if (!IsValid(Counter) || !Counter->CanEmployeeProcessCheckout() ||
			HasActiveTask(ECSStoreEmployeeTaskType::CheckoutService, Counter))
		{
			continue;
		}

		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::CheckoutService;
		Task.Priority = 100;
		Task.TargetActor = Counter;
		Task.RequestedQuantity = 1;
	}
}

void UCSStoreEmployeeSubsystem::RefreshReorganizationTasks()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	// 진열대에 부착된 상품만 검사하므로, 바닥·수레·플레이어가 들고 있는 상품을 직원이 잘못 회수하지 않습니다.
	for (TActorIterator<ACSProductPickupActor> ProductIt(World); ProductIt; ++ProductIt)
	{
		ACSProductPickupActor* Product = *ProductIt;
		ACSShelfActor* SourceShelf = IsEmployeeManageableProduct(Product)
			? Cast<ACSShelfActor>(Product->GetAttachParentActor())
			: nullptr;
		if (!IsValid(SourceShelf) || IsActorHeldByPlayer(World, Product) ||
			HasActiveTaskForSource(Product) ||
			!SourceShelf->ShouldEmployeeReorganizeProduct(Product))
		{
			continue;
		}

		ACSShelfActor* NearestTargetShelf = nullptr;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		bool bUseCrossTierDuplicateFallback = false;
		for (TActorIterator<ACSShelfActor> ShelfIt(World); ShelfIt; ++ShelfIt)
		{
			ACSShelfActor* CandidateShelf = *ShelfIt;
			if (CandidateShelf == SourceShelf ||
				!CanLooseProductRestockShelf(Product, CandidateShelf, false))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(
				Product->GetActorLocation(), CandidateShelf->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				NearestTargetShelf = CandidateShelf;
			}
		}

		if (!IsValid(NearestTargetShelf))
		{
			// 고유 상품을 둘 수 있는 단이 없을 때만, 기존 자동 진열 규칙과 동일하게 같은 상품의 단 간 확장을 허용합니다.
			for (TActorIterator<ACSShelfActor> ShelfIt(World); ShelfIt; ++ShelfIt)
			{
				ACSShelfActor* CandidateShelf = *ShelfIt;
				if (CandidateShelf == SourceShelf ||
					!CanLooseProductRestockShelf(Product, CandidateShelf, true))
				{
					continue;
				}

				const float DistanceSquared = FVector::DistSquared(
					Product->GetActorLocation(), CandidateShelf->GetActorLocation());
				if (DistanceSquared < NearestDistanceSquared)
				{
					NearestDistanceSquared = DistanceSquared;
					NearestTargetShelf = CandidateShelf;
					bUseCrossTierDuplicateFallback = true;
				}
			}
		}

		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct;
		Task.SourceActor = Product;
		Task.SourceFacilityActor = SourceShelf;
		Task.ProductRow = Product->GetProductRow();
		Task.RequestedQuantity = 1;
		Task.bAllowCrossTierDuplicateFallback = bUseCrossTierDuplicateFallback;

		if (IsValid(NearestTargetShelf))
		{
			// 냉장·냉동 오배치는 신선도 손실을 막기 위해 일반 정리보다 높은 우선순위로 처리합니다.
			const FCSProductDataRow* ProductData = Task.ProductRow.GetRow<FCSProductDataRow>(
				TEXT("UCSStoreEmployeeSubsystem::RefreshReorganizationTasks"));
			const bool bIsPerishableStorageError = ProductData != nullptr &&
				(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
					ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
			Task.Priority = bIsPerishableStorageError ? 90 : 70;
			Task.TargetActor = NearestTargetShelf;
			continue;
		}

		ACSFreshnessSafeZoneActor* NearestStoragePoint = nullptr;
		NearestDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<ACSFreshnessSafeZoneActor> StorageIt(World); StorageIt; ++StorageIt)
		{
			ACSFreshnessSafeZoneActor* StoragePoint = *StorageIt;
			if (!IsValid(StoragePoint) || !StoragePoint->CanStoreLooseProduct(Product))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(
				Product->GetActorLocation(), StoragePoint->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				NearestStoragePoint = StoragePoint;
			}
		}

		if (IsValid(NearestStoragePoint))
		{
			Task.Priority = 65;
			Task.TargetActor = NearestStoragePoint;
			continue;
		}

		// 재배치할 진열대와 보관할 창고가 모두 없으면 원래 진열대를 유지하며 불필요한 업무를 만들지 않습니다.
		Tasks.Pop();
		--NextTaskId;
	}
}

void UCSStoreEmployeeSubsystem::RefreshPerishableCrateRestockTasks()
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	for (TActorIterator<ACSDeliveryCrateActor> It(World); It; ++It)
	{
		ACSDeliveryCrateActor* Crate = *It;
		if (!IsValid(Crate) || Crate->GetRemainingQuantity() <= 0 ||
			!IsPerishableProductRow(Crate->GetProductRow()) || IsPerishableCrateSafelyStored(Crate) ||
			HasActiveTaskForSource(Crate) || IsActorHeldByPlayer(World, Crate)) continue;
		ACSShelfActor* TargetShelf = nullptr;
		bool bUseFallback = false;
		// 고유 품목 배치를 우선하고, 없을 때만 기존 중복 단 확장 규칙을 적용합니다.
		for (int32 Pass = 0; Pass < 2 && !IsValid(TargetShelf); ++Pass)
		{
			float NearestDistance = TNumericLimits<float>::Max();
			for (TActorIterator<ACSShelfActor> ShelfIt(World); ShelfIt; ++ShelfIt)
			{
				ACSShelfActor* Shelf = *ShelfIt;
				if (!CanCrateRestockShelf(Crate, Shelf, Pass == 1) ||
					HasPendingReorganizationFromShelf(Shelf) ||
					HasActiveTask(ECSStoreEmployeeTaskType::RestockShelf, Shelf)) continue;
				const float Distance = FVector::DistSquared(Crate->GetActorLocation(), Shelf->GetActorLocation());
				if (Distance < NearestDistance)
				{
					NearestDistance = Distance;
					TargetShelf = Shelf;
					bUseFallback = Pass == 1;
				}
			}
		}
		if (!IsValid(TargetShelf)) continue;
		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::RestockShelf;
		Task.Priority = 100;
		Task.TargetActor = TargetShelf;
		Task.SourceActor = Crate;
		Task.ProductRow = Crate->GetProductRow();
		Task.bAllowCrossTierDuplicateFallback = bUseFallback;
		Task.RequestedQuantity = FMath::Min(Crate->GetRemainingQuantity(),
			TargetShelf->GetEmployeeRestockableSlotCount(Task.ProductRow, bUseFallback));
	}
}

void UCSStoreEmployeeSubsystem::RefreshInboundIntakeTasks()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<ACSDeliveryCrateActor> CrateIt(World); CrateIt; ++CrateIt)
	{
		ACSDeliveryCrateActor* Crate = *CrateIt;
		if (!IsValid(Crate) || !Crate->IsAwaitingStockroomIntake() ||
			Crate->GetRemainingQuantity() <= 0 || HasActiveTaskForSource(Crate) ||
			IsActorHeldByPlayer(World, Crate) ||
			Cast<ACSFreshnessSafeZoneActor>(Crate->GetAttachParentActor()) != nullptr)
		{
			continue;
		}

		ACSFreshnessSafeZoneActor* NearestStoragePoint = nullptr;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<ACSFreshnessSafeZoneActor> StorageIt(World); StorageIt; ++StorageIt)
		{
			ACSFreshnessSafeZoneActor* StoragePoint = *StorageIt;
			if (!IsValid(StoragePoint) || !StoragePoint->CanStoreCrate(Crate, true))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(
				Crate->GetActorLocation(), StoragePoint->GetActorLocation());
			if (!IsValid(NearestStoragePoint) ||
				(StoragePoint->CanStoreCrate(Crate) && !NearestStoragePoint->CanStoreCrate(Crate)) ||
				(StoragePoint->CanStoreCrate(Crate) == NearestStoragePoint->CanStoreCrate(Crate) &&
					DistanceSquared < NearestDistanceSquared))
			{
				NearestDistanceSquared = DistanceSquared;
				NearestStoragePoint = StoragePoint;
			}
		}

		if (!IsValid(NearestStoragePoint))
		{
			// 맞는 창고가 없거나 가득 찬 상자는 다음 진열 업무 생성 단계에서 기존 방식대로 처리합니다.
			continue;
		}

		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::StoreCrate;
		// 발주 입고 상자는 직원 진열 업무보다 먼저 처리해 배송 구역이 빠르게 비워지도록 합니다.
		Task.Priority = 100;
		Task.TargetActor = NearestStoragePoint;
		Task.SourceActor = Crate;
		Task.ProductRow = Crate->GetProductRow();
		Task.RequestedQuantity = Crate->GetRemainingQuantity();
	}
}

void UCSStoreEmployeeSubsystem::RefreshPerishableProtectionTasks()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	// 먼저 생성된 진열 업무가 없는 냉장·냉동 상자는 적합한 창고로 보호합니다.
	// 슬롯이 가득 차면 같은 보관 유형의 안전한 구역 바닥도 사용할 수 있습니다.
	for (TActorIterator<ACSDeliveryCrateActor> CrateIt(World); CrateIt; ++CrateIt)
	{
		ACSDeliveryCrateActor* Crate = *CrateIt;
		if (!IsValid(Crate) || Crate->GetRemainingQuantity() <= 0 ||
			!IsPerishableProductRow(Crate->GetProductRow()) ||
			HasActiveTaskForSource(Crate) || IsActorHeldByPlayer(World, Crate))
		{
			continue;
		}

		if (IsPerishableCrateSafelyStored(Crate))
		{
			continue;
		}

		ACSFreshnessSafeZoneActor* NearestStoragePoint = nullptr;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<ACSFreshnessSafeZoneActor> StorageIt(World); StorageIt; ++StorageIt)
		{
			ACSFreshnessSafeZoneActor* StoragePoint = *StorageIt;
			if (!IsValid(StoragePoint) || !StoragePoint->CanStoreCrate(Crate, true))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(Crate->GetActorLocation(), StoragePoint->GetActorLocation());
			if (!IsValid(NearestStoragePoint) ||
				(StoragePoint->CanStoreCrate(Crate) && !NearestStoragePoint->CanStoreCrate(Crate)) ||
				(StoragePoint->CanStoreCrate(Crate) == NearestStoragePoint->CanStoreCrate(Crate) &&
					DistanceSquared < NearestDistanceSquared))
			{
				NearestDistanceSquared = DistanceSquared;
				NearestStoragePoint = StoragePoint;
			}
		}

		if (!IsValid(NearestStoragePoint))
		{
			continue;
		}

		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::ProtectPerishableStock;
		Task.Priority = 95;
		Task.TargetActor = NearestStoragePoint;
		Task.SourceActor = Crate;
		Task.ProductRow = Crate->GetProductRow();
		Task.RequestedQuantity = Crate->GetRemainingQuantity();
	}

	// 낱개 상품은 빈 냉장·냉동 진열대를 먼저 찾고, 자리가 없을 때만 같은 타입의 창고로 옮깁니다.
	for (TActorIterator<ACSProductPickupActor> ProductIt(World); ProductIt; ++ProductIt)
	{
		ACSProductPickupActor* Product = *ProductIt;
		if (!IsEmployeeManageableProduct(Product) || !IsPerishableProductRow(Product->GetProductRow()) ||
			HasActiveTaskForSource(Product) || IsActorHeldByPlayer(World, Product))
		{
			continue;
		}

		if (IsPerishableProductSafelyStored(Product))
		{
			continue;
		}

		ACSShelfActor* NearestTargetShelf = nullptr;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<ACSShelfActor> ShelfIt(World); ShelfIt; ++ShelfIt)
		{
			ACSShelfActor* Shelf = *ShelfIt;
			if (!CanLooseProductRestockShelf(Product, Shelf, false))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(Product->GetActorLocation(), Shelf->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				NearestTargetShelf = Shelf;
			}
		}

		AActor* TargetActor = NearestTargetShelf;
		if (!IsValid(TargetActor))
		{
			ACSFreshnessSafeZoneActor* NearestStoragePoint = nullptr;
			NearestDistanceSquared = TNumericLimits<float>::Max();
			for (TActorIterator<ACSFreshnessSafeZoneActor> StorageIt(World); StorageIt; ++StorageIt)
			{
				ACSFreshnessSafeZoneActor* StoragePoint = *StorageIt;
				if (!IsValid(StoragePoint) || !StoragePoint->CanStoreLooseProduct(Product))
				{
					continue;
				}

				const float DistanceSquared = FVector::DistSquared(Product->GetActorLocation(), StoragePoint->GetActorLocation());
				if (DistanceSquared < NearestDistanceSquared)
				{
					NearestDistanceSquared = DistanceSquared;
					NearestStoragePoint = StoragePoint;
				}
			}
			TargetActor = NearestStoragePoint;
		}

		if (!IsValid(TargetActor))
		{
			continue;
		}

		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::ProtectPerishableStock;
		Task.Priority = 95;
		Task.TargetActor = TargetActor;
		Task.SourceActor = Product;
		Task.ProductRow = Product->GetProductRow();
		Task.RequestedQuantity = 1;
	}
}

void UCSStoreEmployeeSubsystem::RefreshRestockTasks()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<ACSShelfActor> ShelfIt(World); ShelfIt; ++ShelfIt)
	{
		ACSShelfActor* Shelf = *ShelfIt;
		if (!IsValid(Shelf) || !Shelf->CanEmployeeRestock() || Shelf->GetEmptySlotCount() <= 0 ||
			HasPendingReorganizationFromShelf(Shelf) ||
			HasActiveTask(ECSStoreEmployeeTaskType::RestockShelf, Shelf))
		{
			continue;
		}

		ACSDeliveryCrateActor* NearestMatchingCrate = nullptr;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		bool bUseCrossTierDuplicateFallback = false;
		for (TActorIterator<ACSDeliveryCrateActor> CrateIt(World); CrateIt; ++CrateIt)
		{
			ACSDeliveryCrateActor* Crate = *CrateIt;
			if (!IsValid(Crate) || Crate->GetRemainingQuantity() <= 0 ||
				HasActiveTaskForSource(Crate))
			{
				// 한 상자를 여러 진열 업무가 동시에 예약하면 재고가 중복 사용될 수 있으므로 제외합니다.
				continue;
			}

			if (!CanCrateRestockShelf(Crate, Shelf, false))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(
				Crate->GetActorLocation(), Shelf->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				NearestMatchingCrate = Crate;
			}
		}

		if (!IsValid(NearestMatchingCrate))
		{
			// 고유 품목을 넣을 수 있는 단이 더 이상 없을 때만, 같은 품목을 빈 단에 자동 확장합니다.
			for (TActorIterator<ACSDeliveryCrateActor> CrateIt(World); CrateIt; ++CrateIt)
			{
				ACSDeliveryCrateActor* Crate = *CrateIt;
				if (!IsValid(Crate) || Crate->GetRemainingQuantity() <= 0 ||
					HasActiveTaskForSource(Crate) || !CanCrateRestockShelf(Crate, Shelf, true))
				{
					continue;
				}

				const float DistanceSquared = FVector::DistSquared(Crate->GetActorLocation(), Shelf->GetActorLocation());
				if (DistanceSquared < NearestDistanceSquared)
				{
					NearestDistanceSquared = DistanceSquared;
					NearestMatchingCrate = Crate;
					bUseCrossTierDuplicateFallback = true;
				}
			}
		}

		if (IsValid(NearestMatchingCrate))
		{
			FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
			Task.TaskId = NextTaskId++;
			Task.Type = ECSStoreEmployeeTaskType::RestockShelf;
			Task.Priority = bUseCrossTierDuplicateFallback ? 30 : 50;
			Task.TargetActor = Shelf;
			Task.SourceActor = NearestMatchingCrate;
			Task.ProductRow = NearestMatchingCrate->GetProductRow();
			Task.bAllowCrossTierDuplicateFallback = bUseCrossTierDuplicateFallback;
			// 단별 상품 구성이 있는 경우 전체 빈 슬롯 수가 아니라, 이 상품을 실제로 넣을 수 있는 칸만 요청합니다.
			Task.RequestedQuantity = FMath::Min(
				Shelf->GetEmployeeRestockableSlotCount(
					NearestMatchingCrate->GetProductRow(), bUseCrossTierDuplicateFallback),
				NearestMatchingCrate->GetRemainingQuantity());
			continue;
		}

		// 사용할 상자가 하나도 없을 때만 바닥의 독립 상품을 찾아, 배송 상자 재고를 항상 우선하도록 합니다.
		ACSProductPickupActor* NearestLooseProduct = nullptr;
		NearestDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<ACSProductPickupActor> ProductIt(World); ProductIt; ++ProductIt)
		{
			ACSProductPickupActor* Product = *ProductIt;
			ACSFreshnessSafeZoneActor* StorageParent = IsEmployeeManageableProduct(Product)
				? Cast<ACSFreshnessSafeZoneActor>(Product->GetAttachParentActor())
				: nullptr;
			if (!IsEmployeeManageableProduct(Product) ||
				(IsValid(Product->GetAttachParentActor()) && !IsValid(StorageParent)) ||
				HasActiveTaskForSource(Product))
			{
				// 진열대·수레·플레이어·직원에 붙은 상품은 제외하되, 창고에 보관된 상품은 다시 진열할 수 있도록 허용합니다.
				continue;
			}

			if (!CanLooseProductRestockShelf(Product, Shelf, false))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(
				Product->GetActorLocation(), Shelf->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				NearestLooseProduct = Product;
			}
		}

		if (!IsValid(NearestLooseProduct))
		{
			for (TActorIterator<ACSProductPickupActor> ProductIt(World); ProductIt; ++ProductIt)
			{
				ACSProductPickupActor* Product = *ProductIt;
				if (!IsEmployeeManageableProduct(Product) || IsValid(Product->GetAttachParentActor()) ||
					HasActiveTaskForSource(Product) || !CanLooseProductRestockShelf(Product, Shelf, true))
				{
					continue;
				}

				const float DistanceSquared = FVector::DistSquared(Product->GetActorLocation(), Shelf->GetActorLocation());
				if (DistanceSquared < NearestDistanceSquared)
				{
					NearestDistanceSquared = DistanceSquared;
					NearestLooseProduct = Product;
					bUseCrossTierDuplicateFallback = true;
				}
			}
		}

		if (!IsValid(NearestLooseProduct))
		{
			continue;
		}

		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::RestockShelf;
		Task.Priority = bUseCrossTierDuplicateFallback ? 20 : 40;
		Task.TargetActor = Shelf;
		Task.SourceActor = NearestLooseProduct;
		Task.ProductRow = NearestLooseProduct->GetProductRow();
		Task.bAllowCrossTierDuplicateFallback = bUseCrossTierDuplicateFallback;
		Task.RequestedQuantity = 1;
	}
}

void UCSStoreEmployeeSubsystem::RefreshStorageTasks()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<ACSDeliveryCrateActor> CrateIt(World); CrateIt; ++CrateIt)
	{
		ACSDeliveryCrateActor* Crate = *CrateIt;
		if (!IsValid(Crate) || Crate->GetRemainingQuantity() <= 0 ||
			HasActiveTaskForSource(Crate) ||
			Crate->IsAwaitingStockroomIntake() ||
			Cast<ACSFreshnessSafeZoneActor>(Crate->GetAttachParentActor()) != nullptr)
		{
			// 이미 다른 직원이 사용 중이거나 창고에 보관된 상자는 새 보관 업무를 만들지 않습니다.
			continue;
		}

		bool bHasAvailableShelf = false;
		for (TActorIterator<ACSShelfActor> ShelfIt(World); ShelfIt; ++ShelfIt)
		{
			if (CanCrateRestockShelf(Crate, *ShelfIt))
			{
				bHasAvailableShelf = true;
				break;
			}
		}

		if (bHasAvailableShelf)
		{
			// 진열할 위치가 남아 있으면 창고보다 진열 업무가 우선입니다.
			continue;
		}

		ACSFreshnessSafeZoneActor* NearestStoragePoint = nullptr;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<ACSFreshnessSafeZoneActor> StorageIt(World); StorageIt; ++StorageIt)
		{
			ACSFreshnessSafeZoneActor* StoragePoint = *StorageIt;
			if (!IsValid(StoragePoint) || !StoragePoint->CanStoreCrate(Crate, true))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(
				Crate->GetActorLocation(), StoragePoint->GetActorLocation());
			if (!IsValid(NearestStoragePoint) ||
				(StoragePoint->CanStoreCrate(Crate) && !NearestStoragePoint->CanStoreCrate(Crate)) ||
				(StoragePoint->CanStoreCrate(Crate) == NearestStoragePoint->CanStoreCrate(Crate) &&
					DistanceSquared < NearestDistanceSquared))
			{
				NearestDistanceSquared = DistanceSquared;
				NearestStoragePoint = StoragePoint;
			}
		}

		if (!IsValid(NearestStoragePoint))
		{
			continue;
		}

		FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
		Task.TaskId = NextTaskId++;
		Task.Type = ECSStoreEmployeeTaskType::StoreCrate;
		Task.Priority = 25;
		Task.TargetActor = NearestStoragePoint;
		Task.SourceActor = Crate;
		Task.ProductRow = Crate->GetProductRow();
		Task.RequestedQuantity = Crate->GetRemainingQuantity();
	}

	// 상자가 우선 보관된 뒤에도 진열할 곳이 없는 낱개 상품은 자동 격자 보관 구역으로 옮깁니다.
	for (TActorIterator<ACSProductPickupActor> ProductIt(World); ProductIt; ++ProductIt)
	{
		ACSProductPickupActor* Product = *ProductIt;
		if (!IsEmployeeManageableProduct(Product) || IsValid(Product->GetAttachParentActor()) ||
			HasActiveTaskForSource(Product))
		{
			continue;
		}

		bool bHasAvailableShelf = false;
		for (TActorIterator<ACSShelfActor> ShelfIt(World); ShelfIt; ++ShelfIt)
		{
			if (CanLooseProductRestockShelf(Product, *ShelfIt))
			{
				bHasAvailableShelf = true;
				break;
			}
		}
		if (bHasAvailableShelf)
		{
			continue;
		}

		ACSFreshnessSafeZoneActor* NearestStoragePoint = nullptr;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<ACSFreshnessSafeZoneActor> StorageIt(World); StorageIt; ++StorageIt)
		{
			ACSFreshnessSafeZoneActor* StoragePoint = *StorageIt;
			if (!IsValid(StoragePoint) || !StoragePoint->CanStoreLooseProduct(Product))
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(
				Product->GetActorLocation(), StoragePoint->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				NearestStoragePoint = StoragePoint;
			}
		}

		if (IsValid(NearestStoragePoint))
		{
			FCSStoreEmployeeTask& Task = Tasks.AddDefaulted_GetRef();
			Task.TaskId = NextTaskId++;
			Task.Type = ECSStoreEmployeeTaskType::StoreLooseProduct;
			Task.Priority = 20;
			Task.TargetActor = NearestStoragePoint;
			Task.SourceActor = Product;
			Task.ProductRow = Product->GetProductRow();
			Task.RequestedQuantity = 1;
		}
	}
}

bool UCSStoreEmployeeSubsystem::HasActiveTask(
	ECSStoreEmployeeTaskType Type,
	const AActor* TargetActor) const
{
	return Tasks.ContainsByPredicate([Type, TargetActor](const FCSStoreEmployeeTask& Task)
	{
		return Task.Type == Type && Task.TargetActor == TargetActor &&
			(Task.Status == ECSStoreEmployeeTaskStatus::Pending ||
				Task.Status == ECSStoreEmployeeTaskStatus::InProgress);
	});
}

bool UCSStoreEmployeeSubsystem::HasActiveTaskForSource(const AActor* SourceActor) const
{
	return IsValid(SourceActor) && Tasks.ContainsByPredicate([SourceActor](const FCSStoreEmployeeTask& Task)
	{
		return Task.SourceActor == SourceActor &&
			(Task.Status == ECSStoreEmployeeTaskStatus::Pending ||
				Task.Status == ECSStoreEmployeeTaskStatus::InProgress);
	});
}

bool UCSStoreEmployeeSubsystem::HasPendingReorganizationFromShelf(const ACSShelfActor* Shelf) const
{
	return IsValid(Shelf) && Tasks.ContainsByPredicate([Shelf](const FCSStoreEmployeeTask& Task)
	{
		return Task.Type == ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct &&
			Task.SourceFacilityActor == Shelf &&
			(Task.Status == ECSStoreEmployeeTaskStatus::Pending ||
				Task.Status == ECSStoreEmployeeTaskStatus::InProgress);
	});
}
