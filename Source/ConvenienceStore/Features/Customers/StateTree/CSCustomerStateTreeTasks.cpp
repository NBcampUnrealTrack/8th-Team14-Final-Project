#include "Features/Customers/StateTree/CSCustomerStateTreeTasks.h"
#include "StateTreeExecutionContext.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
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

			// 실제로 비교 대상에 오르는 개수는 상위 TopCandidateCount개로 잘리므로(ScoreAndSelectTopCandidates)
			// 봤다고 칠 개수도 진열대 상품 총개수가 아니라 그 상한을 넘지 않도록 함
			const int32 EvaluatedCount = FMath::Min(Candidates.Num(), Decision->GetEffectiveTopCandidateCount());

			// 이 진열대에서 실제로 비교한 상품 개수만큼 감소 (0 밑으로는 안 내려감)
			State->RemainingEvaluationBudget = FMath::Max(State->RemainingEvaluationBudget - EvaluatedCount, 0);

			// 후보 중 하나를 확률적으로 선택하거나 다 별로면 미구매(false) 반환
			FCSPurchaseCandidate Selected;
			const bool bPicked = Decision->DecidePurchase(Candidates, Selected);

			UE_LOG(LogTemp, Log, TEXT("[카테고리 선호 파악용] %s | %s"),
				Decision->Archetype ? *Decision->Archetype->DisplayName.ToString() : TEXT("종족없음"),
				bPicked ? *Selected.ProductRowHandle.RowName.ToString() : TEXT("미구매"));
			if (bPicked)
			{
				Subsystem->TakeProduct(ShelfActor, Selected.ProductActor, Customer);
			}
		}
	}
	else
	{
		// 진열대가 비어있어도 와서 확인하는 데는 시간이 들었다고 보고 최소 1만큼 차감
		State->RemainingEvaluationBudget = FMath::Max(State->RemainingEvaluationBudget - 1, 0);
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

// =====================================================================
// PrepareForcedLeave
// =====================================================================

EStateTreeRunStatus FCSTask_PrepareForcedLeave::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem =
		Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	if (Subsystem != nullptr)
	{
		Subsystem->PrepareForcedLeave(Data.Actor);
	}

	// 서브시스템이 없어도 성공으로 넘김
	UE_LOG(LogTemp, Log, TEXT("[영업종료퇴장] %s"), *Data.Actor->GetName());

	return EStateTreeRunStatus::Succeeded;
}

// =====================================================================
// SubduedDespawn
// =====================================================================

EStateTreeRunStatus FCSTask_SubduedDespawn::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 이전 상태의 ExitState가 이미 StopMovement를 불렀지만,
	// 어느 상태에서 들어왔는지에 의존하지 않도록 여기서도 한 번 확실히 끊는다.
	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}

	// Shoplift가 올려둔 도주 속도가 남아 있어도 밀려나지 않게 이동 자체를 막는다.
	if (UCharacterMovementComponent* Movement = Data.Actor->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	Data.ElapsedTime = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("[제압] %s 소멸 대기 시작"), *Data.Actor->GetName());

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_SubduedDespawn::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	UCSCustomerRuntimeSubsystem* Subsystem =
		Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	// 파라미터가 0이면 서브시스템의 공용 값을 쓴다. 서브시스템까지 없으면 최후 폴백.
	float DespawnDelay = Data.DespawnDelayOverride;
	if (DespawnDelay <= 0.0f)
	{
		DespawnDelay = (Subsystem != nullptr) ? Subsystem->SubduedDespawnDelay : 3.0f;
	}

	Data.ElapsedTime += DeltaTime;
	if (Data.ElapsedTime < DespawnDelay)
	{
		return EStateTreeRunStatus::Running;
	}

	// TODO(제압 연출): 랙돌/몬타주 작업 시 주석 해제. 상세는 VillainSubdue_Design.md 4-6절
	// 연출 정리 자리. 랙돌이 들어오더라도 소멸 시점은 아래 그대로 유지된다.
	//Data.Actor->EndSubduedReaction();

	// 통계 방송. 제압 시점에 상품 목록이 비워졌으므로
	// 이 안의 ReportShopliftEscaped는 손실을 방송하지 않고 조용히 빠져나간다.
	if (Subsystem != nullptr)
	{
		Subsystem->NotifyCustomerDespawned(Data.Actor);
	}

	// Destroyed()가 AIController와 장바구니, 봉투까지 정리한다.
	Data.Actor->Destroy();

	return EStateTreeRunStatus::Succeeded;
}

// =====================================================================
// ApproachStorefront
// =====================================================================

EStateTreeRunStatus FCSTask_ApproachStorefront::EnterState(FStateTreeExecutionContext& Context,
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

	// [ForceLeave 연동 가드]
	// 이미 강제 퇴장이 확정된 손님은 가게 앞으로 되돌아가면 안 된다.
	//
	// 이 가드가 필요한 경로: Leave 상태가 Failed를 반환하면 Root로 돌아가는데,
	// 그때 ForceLeave는 bForcedToLeave 때문에 false가 되고
	// Approach는 HasEnteredStore가 false라 true가 되어 여기로 들어온다.
	// 그러면 마감 시각에 손님이 가게 앞으로 걸어가게 됨
	if (State->IsForcedToLeave())
	{
		return EStateTreeRunStatus::Failed;
	}

	// 판정 지점이 없을 때 조용히 입장시키지 않는다.
	// 조용히 통과시키면 개폐 판정이 통째로 사라지는데 로그가 없어 원인 추적이 불가능하다.
	// (스폰 초기화 순서 버그가 실제로 이 폴백에 가려 오래 발견되지 않았다.)
	if (State->StorefrontLocation.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[입장판정] %s StorefrontLocation이 비어 있음 - 레벨에 StorefrontPoint를 배치했는지, "
				 "스포너가 StateTree 시작 전에 값을 주입하는지 확인 필요"),
			*Data.Actor->GetName());

		return EStateTreeRunStatus::Failed;
	}

	// 반환값을 확인한다. 경로 요청이 실패하면 손님은 제자리에서 판정만 반복해
	// "가게 앞까지 못 간다"로 보이는데, 조용히 넘기면 원인 추적이 불가능하다.
	// (목표 좌표를 NavMesh에 투영하지 못하면 여기서 Failed가 나온다)
	const EPathFollowingRequestResult::Type MoveResult =
		Data.AIController->MoveToLocation(State->StorefrontLocation, Data.AcceptanceRadius);

	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[입장판정] %s 가게 앞으로 가는 경로 요청 실패 - 목표 %s. "
				 "판정 지점이 NavMesh 위에 있는지, 목표 Z가 지면 높이인지 확인 필요"),
			*Data.Actor->GetName(), *State->StorefrontLocation.ToCompactString());

		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_ApproachStorefront::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (Data.Actor == nullptr || Data.AIController == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 아직 걸어가는 중이면 대기
	if (Data.AIController->GetMoveStatus() != EPathFollowingStatus::Idle)
	{
		return EStateTreeRunStatus::Running;
	}

	UCSCustomerRuntimeSubsystem* Subsystem =
		Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
	if (Subsystem == nullptr || State == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 가게 앞에 도착했으니 한 번 판정
	if (Subsystem->ShouldEnterStore(Data.Actor) == false)
	{
		UE_LOG(LogTemp, Log, TEXT("[입장판정] %s 그냥 지나감"), *Data.Actor->GetName());
		return EStateTreeRunStatus::Failed;
	}

	// 진입 확정
	//  - 통계에서 행인과 구분되고
	//  - 매장 안 손님이 다시 가게 앞으로 나가지 않는다.
	State->MarkEnteredStore();

	UE_LOG(LogTemp, Log, TEXT("[입장판정] %s 입장"), *Data.Actor->GetName());

	return EStateTreeRunStatus::Succeeded;
}

void FCSTask_ApproachStorefront::ExitState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 다음 상태가 새 이동 명령을 내리기 전에 진행 중인 경로를 정리한
	// ForceLeave가 이 상태를 중간에 끊을 때도 이 함수가 먼저 실행
	// PrepareForcedLeave 태스크는 StopMovement를 따로 부르지 않아도 된다.
	if (Data.AIController != nullptr)
	{
		Data.AIController->StopMovement();
	}
}
