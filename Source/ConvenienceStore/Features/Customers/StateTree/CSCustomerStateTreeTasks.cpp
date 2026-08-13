#include "Features/Customers/StateTree/CSCustomerStateTreeTasks.h"
#include "StateTreeExecutionContext.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "GameFramework/CharacterMovementComponent.h"

// =====================================================================
// PickNextShelf
// =====================================================================

EStateTreeRunStatus FCSTask_PickNextShelf::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (Subsystem == nullptr || State == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* PickedShelf = Subsystem->PickRandomUnvisitedShelf(State->VisitedShelves);
	if (PickedShelf == nullptr)
	{
		// 더 볼 진열대가 없음 → 상태 실패 → 전이(On Failed)로 다음 단계 결정
		return EStateTreeRunStatus::Failed;
	}

	Data.TargetShelf = PickedShelf;
	Data.TargetLocation = Subsystem->GetShelfBrowseLocation(PickedShelf, Data.BrowseDistance);

	// Succeeded를 반환하지 않고 Running을 반환합니다.
	// 그래야 이 상태가 종료되지 않고 계속 유지되면서 자식 상태(MoveTo, Browse)들이
	// 이 태스크의 Output 데이터(TargetShelf, TargetLocation)를 바인딩해서 읽어갈 수 있음
	return EStateTreeRunStatus::Running;
}

// =====================================================================
// MoveToLocation
// =====================================================================

EStateTreeRunStatus FCSTask_MoveToLocation::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* Controller = Data.AIController;
	if (Controller == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	const EPathFollowingRequestResult::Type Result =
		Controller->MoveToLocation(Data.TargetLocation, Data.AcceptanceRadius);

	if (Result == EPathFollowingRequestResult::Failed)
	{
		// 경로가 없거나 NavMesh 밖일 때 등 실패 반환
		return EStateTreeRunStatus::Failed;
	}
	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// 이미 목표 위치에 있으면 즉시 성공
		return EStateTreeRunStatus::Succeeded;
	}
	// 이동이 시작되면 Tick에서 검사를 이어가기 위해 Running 반환
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_MoveToLocation::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* Controller = Data.AIController;
	if (Controller == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 단순 버전의 MoveToLocation이라 완료 콜백이 없으므로 매 틱 폴링하여 이동 종료를 판단합니다
	if (Controller->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		const APawn* Pawn = Controller->GetPawn();
		if (Pawn != nullptr)
		{
			const float Dist2D = FVector::Dist2D(Pawn->GetActorLocation(), Data.TargetLocation);

			// 캐릭터가 목표에 정확히 멈추지 못했다고  실패 처리되는 현상을 막기 위해 +100.0f 만큼의 거리를 허용
			if (Dist2D <= Data.AcceptanceRadius + 100.0f)
			{
				return EStateTreeRunStatus::Succeeded;
			}
		}
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

void FCSTask_MoveToLocation::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (Data.AIController != nullptr)
	{
		// 상태를 떠날 때 이동 명령이 남아있으면 다음 상태에서도 걸어갈 수 있으므로 반드시 끊어야 함
		Data.AIController->StopMovement();
	}
}

// =====================================================================
// BrowseShelf
// =====================================================================

EStateTreeRunStatus FCSTask_BrowseShelf::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 재진입할 때마다 타이머 리셋이 필요하므로 여기서 0으로 초기화
	Data.ElapsedTime = 0.0f;

	if (Data.Actor == nullptr || Data.TargetShelf == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_BrowseShelf::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.ElapsedTime += DeltaTime;

	// BrowseTime에 도달할 때까지 Running 유지
	if (Data.ElapsedTime < Data.BrowseTime)
	{
		return EStateTreeRunStatus::Running;
	}

	// 구경 끝 → 방문 처리 + 구매 판단
	ACSCustomerCharacter* Customer = Data.Actor;
	AActor* ShelfActor = Data.TargetShelf;
	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	if (State == nullptr || Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}
	// 구매를 하든 안하든 방문 처리는 해야 같은 진열대를 무한히 맴돌지 않으므로 항상 기록
	State->MarkVisited(ShelfActor);

	if (Subsystem->HasShelfStock(ShelfActor))
	{
		// 이 손님에게 판단 컴포넌트가 붙어있지 않으면 구매 판단 자체를 건너뛰고 지나감
		UCSPurchaseDecisionComponent* Decision = Customer->FindComponentByClass<UCSPurchaseDecisionComponent>();
		if (Decision != nullptr)
		{
			// 진열대 위 상품들을 판단용 후보 목록으로 변환
			const TArray<FCSPurchaseCandidate> Candidates = Subsystem->BuildPurchaseCandidates(ShelfActor);

			// 이 진열대에서 살펴본 상품 개수만큼 감소 (0 밑으로는 안 내려감)
			State->RemainingEvaluationBudget = FMath::Max(State->RemainingEvaluationBudget - Candidates.Num(), 0);

			// 후보 중 하나를 확률적으로 선택하거나 다 별로면 미구매(false) 반환
			FCSPurchaseCandidate Selected;

			if (Decision->DecidePurchase(Candidates, Selected))
			{
				// 결정된 상품을 실제로 집음 (재고 차감 + 장바구니 기록은 TakeProduct 내부 처리)
				Subsystem->TakeProduct(ShelfActor, Selected.ProductActor, Customer);
			}
		}
	}

	// 사든 말든 현재 진열대 확인 완료 → 다음 행동 결정
	return EStateTreeRunStatus::Succeeded;
}

// =====================================================================
// WaitInQueue
// =====================================================================

EStateTreeRunStatus FCSTask_WaitInQueue::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 대기열 등록 후 내 순번 위치로 이동
	const int32 MyIndex = Subsystem->JoinQueue(Data.Actor);
	if (MyIndex < 0)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.CachedQueueIndex = MyIndex;
	Data.ElapsedWaitTime = 0.0f; //손님이 대기열 상태에 새로 진입할 때마다 타이머를 0초부터 다시 시작하도록 초기화하는 역할 (혹시 모를 재진입시)
	Data.AIController->MoveToLocation(Subsystem->GetQueueSlotLocation(MyIndex), 30.0f);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_WaitInQueue::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UCSCustomerStateComponent* State = nullptr;
	if (Data.Actor != nullptr)
	{
		State = Data.Actor->GetCustomerState();
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	if (State == nullptr || Subsystem == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 계산 완료 → 성공 (계산대의 CompleteFrontCheckout이 플래그 킴)
	if (State->bCheckoutDone)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 인내심 판정 (시간 오버되면 포기하고 Leave)
	Data.ElapsedWaitTime += DeltaTime;

	float Patience = Data.PatienceOverride;
	if (Patience <= 0.0f)
	{
		// 태스크에서 따로 지정하지 않았으면 Subsystem이 주는 값을 쓴다.
		// 소비심리 파트가 붙으면 그 함수 내부만 바뀌므로 여기는 그대로 둔다.
		Patience = Subsystem->GetPatienceSeconds(Data.Actor);
	}

	if (Patience > 0.0f && Data.ElapsedWaitTime >= Patience)
	{
		// 대기열 이탈, 상품 반환, 방송까지 Subsystem이 한 번에 처리한다.
		Subsystem->AbandonCheckout(Data.Actor, Data.ElapsedWaitTime);

		// 실패로 종료 → Checkout 상태의 On Failed 전이가 Leave로 보낸다.
		return EStateTreeRunStatus::Failed;
	}
	//

	// 줄이 줄어들어 내 순번이 바뀌었으면 앞자리로 이동
	const int32 CurrentIndex = Subsystem->GetQueueIndex(Data.Actor);
	if (CurrentIndex >= 0 && CurrentIndex != Data.CachedQueueIndex)
	{
		// 순번 인덱스 캐시와 다를 때만 갱신 후 이동합니다
		Data.CachedQueueIndex = CurrentIndex;
		Data.AIController->MoveToLocation(Subsystem->GetQueueSlotLocation(CurrentIndex), 30.0f);
	}

	return EStateTreeRunStatus::Running;
}

void FCSTask_WaitInQueue::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 계산 없이 상태를 떠나는 경우(중도 이탈) 대기열 정리 - 안전장치
	UCSCustomerStateComponent* State = nullptr;
	if (Data.Actor != nullptr)
	{
		State = Data.Actor->GetCustomerState();
	}

	if (State != nullptr && State->bCheckoutDone == false)
	{
		UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
		if (Subsystem != nullptr)
		{
			Subsystem->LeaveQueue(Data.Actor);
		}
	}

	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}
}

// =====================================================================
// LeaveAndDespawn
// =====================================================================

EStateTreeRunStatus FCSTask_LeaveAndDespawn::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (State == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	const EPathFollowingRequestResult::Type Result =
		Data.AIController->MoveToLocation(State->ExitLocation, Data.AcceptanceRadius);

	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// 빈손 퇴장률 집계용 : 파괴 직전에 구매 완료 여부를 방송
		if (Subsystem != nullptr)
		{
			Subsystem->NotifyCustomerDespawned(Data.Actor);
		}

		Data.Actor->Destroy();
		return EStateTreeRunStatus::Succeeded;
	}
	if (Result == EPathFollowingRequestResult::Failed)
	{
		// 경로를 못 찾아도 남겨두면 유령 손님이 되므로 그냥 제거
		if (Subsystem != nullptr)
		{
			Subsystem->NotifyCustomerDespawned(Data.Actor);
		}
		Data.Actor->Destroy();
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_LeaveAndDespawn::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (Data.AIController->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		// 빈손 퇴장률 집계용 : 파괴 직전에 구매 완료 여부를 방송
		if (UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			Subsystem->NotifyCustomerDespawned(Data.Actor);
		}

		// 이동이 완료되면 도달 여부와 무관하게 무조건 삭제
		Data.Actor->Destroy();
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

// =====================================================================
// ReturnProducts
// =====================================================================

EStateTreeRunStatus FCSTask_ReturnProducts::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 빈자리 여부와 무관하게 진열대 하나를 선택
	Data.TargetShelf = Subsystem->PickRandomShelf();

	// 예외처리) 레벨에 진열대가 하나도 없으면 갈 곳이 없다. 그 자리에 떨구고 끝낸다.
	if (Data.TargetShelf == nullptr)
	{
		Subsystem->DropCustomerItems(Data.Actor);
		return EStateTreeRunStatus::Succeeded;
	}

	const FVector TargetLocation =
		Subsystem->GetShelfBrowseLocation(Data.TargetShelf, Data.BrowseDistance);

	const EPathFollowingRequestResult::Type Result =
		Data.AIController->MoveToLocation(TargetLocation, Data.AcceptanceRadius);

	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		Subsystem->ReturnCustomerItemsToShelf(Data.Actor, Data.TargetShelf);
		return EStateTreeRunStatus::Succeeded;
	}
	if (Result == EPathFollowingRequestResult::Failed)
	{
		// 예외처리) 경로를 못 찾으면 붙잡아두지 말고 그 자리에 떨어뜨림
		Subsystem->DropCustomerItems(Data.Actor);
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_ReturnProducts::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 이동이 끝날 때까지 대기
	if (Data.AIController->GetMoveStatus() != EPathFollowingStatus::Idle)
	{
		return EStateTreeRunStatus::Running;
	}

	// 길을 막혀서 정확히 도착하지 못하고 멈춰버리는 경우에 일단 되돌리기 기능을 시도는 함
	Subsystem->ReturnCustomerItemsToShelf(Data.Actor, Data.TargetShelf);

	return EStateTreeRunStatus::Succeeded;
}

void FCSTask_ReturnProducts::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}

	// 어떤 경로로든 상품이 남은 채 이 상태를 떠나면 그 자리에 떨군다.
	// 남겨두면 손님이 상품을 든 채로 소멸해 재고가 증발. (나중에 손님유형따라 든 채로 가는것도 컨텐츠 가능)
	if (Data.Actor == nullptr)
	{
		return;
	}

	const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (State != nullptr && State->HasSelectedItems())
	{
		UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
		if (Subsystem != nullptr)
		{
			Subsystem->DropCustomerItems(Data.Actor);
		}
	}
}

EStateTreeRunStatus FCSTask_Shoplift::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// bVillainActionDone 표시 + 경고 방송을 서브시스템이 처리
	Subsystem->CommitShoplift(Data.Actor);

	// 도주 연출. 이 값은 상태를 떠나도 유지되므로 Leave까지 빠른 걸음이 이어집니다.
	if (UCharacterMovementComponent* Movement = Data.Actor->GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = Data.FleeWalkSpeed;
	}

	// 이동은 Leave 상태가 담당하므로 여기서는 즉시 성공을 반환
	return EStateTreeRunStatus::Succeeded;
}
