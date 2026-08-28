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
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"

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

	// MoveToShelf 이동 실패로 재진입한 경우 처리 (ST_Customer: MoveToShelf의 Failed 전이가 Shopping을 다시 겨냥함)
	// 직전에 골랐던 진열대(Data.TargetShelf)가 아직 VisitedShelves에 없다는 건
	// Browse까지 가보지 못했다(= MarkVisited를 부를 기회가 없었다 = MoveToShelf가 실패했다)는 신호
	// 이동 성공/실패와 무관하게 시도한 진열대는 여기서 방문 처리
	// 이게 없으면 내비메시가 막힌 진열대가 계속 안 가본 곳으로 남아 재선택되고
	// 손님 하나가 같은 진열대에 수백 번 재시도하며 MaxAlive 슬롯을 점유해 신규 스폰을 막음
	if (Data.TargetShelf != nullptr && State->HasVisited(Data.TargetShelf) == false)
	{
		State->MarkVisited(Data.TargetShelf);
	}

	AActor* PickedShelf = Subsystem->PickRandomUnvisitedShelf(State->VisitedShelves);
	if (PickedShelf == nullptr)
	{
		// 더 볼 진열대가 없음 → 상태 실패 → 전이(On Failed)로 다음 단계 결정
		return EStateTreeRunStatus::Failed;
	}

	// 설 자리는 진열대의 CustomerShoppingPoint 마커에서 받아옵니다.
	FVector BrowseLocation;
	FRotator BrowseFacing;
	float BrowseRadius = 0.0f;
	if (!Subsystem->TryGetShelfBrowsePoint(PickedShelf, BrowseLocation, BrowseFacing, BrowseRadius))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BrowseZone] %s: CustomerShoppingPoint 주변에 NavMesh가 없어 갈 수 없습니다. "
				 "마커를 NavObstacleBox 바깥의 걸을 수 있는 바닥 위로 옮기세요."),
			*PickedShelf->GetName());
		State->MarkVisited(PickedShelf);
		return EStateTreeRunStatus::Failed;
	}

	Data.TargetShelf = PickedShelf;
	Data.TargetLocation = BrowseLocation;

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

	// 진열대로 가는 이동이면 진열대가 정한 영역 반경 안에서 멈춤
	float StopRadius = Data.AcceptanceRadius;
	if (Data.TargetShelf != nullptr)
	{
		if (const UCSCustomerRuntimeSubsystem* Subsystem =
			Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>())
		{
			FVector ZoneCenter;
			FRotator ZoneFacing;
			float ZoneRadius = 0.0f;
			if (Subsystem->TryGetShelfBrowsePoint(Data.TargetShelf, ZoneCenter, ZoneFacing, ZoneRadius))
			{
				StopRadius = ZoneRadius;
			}
		}
	}

	const EPathFollowingRequestResult::Type Result = Controller->MoveToLocation(Data.TargetLocation, StopRadius, false);

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
		if (Pawn == nullptr)
		{
			return EStateTreeRunStatus::Failed;
		}

		// 진열대로 가는 이동이면 "그 진열대의 영역(마커 중심 · BrowseZoneRadius) 안에 멈췄는가"가
		// 유일한 성공 조건입니다. 거리로 판정하면 진열대마다 기준이 달라집니다.
		if (Data.TargetShelf != nullptr)
		{
			const UCSCustomerRuntimeSubsystem* Subsystem =
				Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
			if (Subsystem == nullptr)
			{
				return EStateTreeRunStatus::Failed;
			}

			// 손님이 진열대의 구경 영역 안에 있는지 확인.
			if (Subsystem->IsCustomerInsideBrowseZone(Data.TargetShelf, Pawn))
			{
				// 영역 안에 들어왔다면 이동 성공으로 판정
				return EStateTreeRunStatus::Succeeded;
			}
			else
			{
				// 영역 밖에 있다면 이동 실패로 판정
				return EStateTreeRunStatus::Failed;
			}
		}

		// 계산대·출구처럼 영역이 없는 이동은 기존 거리 판정을 사용
		const float Dist2D = FVector::Dist2D(Pawn->GetActorLocation(), Data.TargetLocation);

		// 손님의 현재 위치와 목표 위치 사이의 평면 거리가 허용 반경 이내인지 확인
		if (Dist2D <= Data.AcceptanceRadius)
		{
			// 허용 반경 안으로 들어왔다면 이동 성공으로 판정
			return EStateTreeRunStatus::Succeeded;
		}
		else
		{
			// 허용 반경 밖에서 멈췄다면 이동 실패로 판정
			return EStateTreeRunStatus::Failed;
		}
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

	// 재진입할 때마다 타이머/세션 상태 리셋이 필요하므로 여기서 초기화
	// (인스턴스 데이터는 재진입 사이에 값이 남아있으므로 명시적으로 0으로 되돌려야 함)
	Data.ElapsedTime = 0.0f;
	Data.RoundCount = 0;
	Data.bBudgetCharged = false;

	if (Data.Actor == nullptr || Data.TargetShelf == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

// 거절 즉시표시(DrawDebugString) + LastRejectionInfo/LastRejectionTime 기록 +
// NotifyPurchaseRejected 방송을 한 번에 처리하는 공용 절차. 빈 진열대 분기와 정상 거절 분기가 동일하게 씀
static void CS_HandlePurchaseRejection(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	UCSCustomerStateComponent& State,
	UCSCustomerRuntimeSubsystem& Subsystem,
	UCSPurchaseDecisionComponent& Decision,
	const FCSPurchaseRejectionInfo& RejectionInfo,
	UWorld* World)
{
	const float DisplayDuration = Decision.GetInstantDisplayDuration(RejectionInfo.Reason);
	State.LastRejectionInfo = RejectionInfo;
	State.LastRejectionTime = World != nullptr ? World->GetTimeSeconds() : 0.0f;

	DrawDebugString(
		World,
		Customer.GetActorLocation() + FVector(0.0f, 0.0f, Customer.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0f),
		Decision.GetRejectionDisplayTier(RejectionInfo).DebugText.ToString(),
		nullptr, FColor::White, DisplayDuration, true);

	Subsystem.NotifyPurchaseRejected(&Customer, RejectionInfo);

	Data.bAwaitingRejectionExit = true;
	Data.ElapsedTime = 0.0f;
}

// 구매는 결정됐지만 가격에 대한 반응이 있을 때
// 즉시 담지 않고 멈춰서 디버그 텍스트 먼저 보여준 뒤 대기가 끝나면 그때 담음
static void CS_HandlePriceReactionThenBuy(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	UCSCustomerStateComponent& State,
	UCSPurchaseDecisionComponent& Decision,
	const FCSPurchaseRejectionInfo& ReactionInfo,
	AActor* ProductActor,
	UWorld* World)
{
	const float DisplayDuration = Decision.GetInstantDisplayDuration(ReactionInfo.Reason);
	State.LastRejectionInfo = ReactionInfo;
	State.LastRejectionTime = World != nullptr ? World->GetTimeSeconds() : 0.0f;

	DrawDebugString(
		World,
		Customer.GetActorLocation() + FVector(0.0f, 0.0f, Customer.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0f),
		Decision.GetRejectionDisplayTier(ReactionInfo).DebugText.ToString(),
		nullptr, FColor::White, DisplayDuration, true);

	Data.bAwaitingRejectionExit = true;
	Data.bPendingTakeProductAfterReaction = true;
	Data.PendingProductActor = ProductActor;
	Data.ElapsedTime = 0.0f;
}

// 상품을 담은 직후(즉시 담았든, 가격 반응 대기 후에 담았든) 공통으로 수행하는 세션 지속 여부 판단
// 라운드상한 -> 목표달성 -> 예산바닥 -> 확률탈락 순으로 검사, 전부 통과하면 다음 라운드로 Running 반환
// 두 진입 경로(즉시 구매 / 대기 후 구매)가 이 판단을 중복 작성하지 않도록 뽑아냄
static EStateTreeRunStatus CS_EvaluateSessionContinuation(
	FCSTask_BrowseShelfInstanceData& Data,
	ACSCustomerCharacter& Customer,
	UCSCustomerStateComponent& State,
	UCSPurchaseDecisionComponent& Decision)
{
	// 안전 상한 - 무한 루프 방지용 순수 방어 코드
	if (Data.RoundCount >= Data.MaxSessionRounds)
	{
		// [세션종료 확인용] 어느 종료조건이 실제로 자주 걸리는지 확인용
		UE_LOG(LogTemp, Log, TEXT("[세션종료 확인용] %s | 사유=라운드상한 | 담은개수=%d/%d | RoundCount=%d"),
			*Customer.GetName(), State.SelectProducts.Num(), State.PlannedBasketTarget, Data.RoundCount);
		return EStateTreeRunStatus::Succeeded;
	}

	// 목표 개수(PlannedBasketTarget)를 채웠으면 세션 종료
	const int32 ItemsInBasket = State.SelectProducts.Num();
	if (ItemsInBasket >= State.PlannedBasketTarget)
	{
		UE_LOG(LogTemp, Log, TEXT("[세션종료 확인용] %s | 사유=목표달성 | 담은개수=%d/%d"),
			*Customer.GetName(), ItemsInBasket, State.PlannedBasketTarget);
		return EStateTreeRunStatus::Succeeded;
	}

	// 소프트/하드 예산 중 하나라도 바닥나면 더 둘러봐도 살 수 없으므로 종료
	if (State.GetRemainingSoftBudget() <= 0 || State.GetRemainingHardWallet() <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[세션종료 확인용] %s | 사유=예산바닥 | 담은개수=%d/%d | 남은소프트=%d | 남은하드=%d"),
			*Customer.GetName(), ItemsInBasket, State.PlannedBasketTarget,
			State.GetRemainingSoftBudget(), State.GetRemainingHardWallet());
		return EStateTreeRunStatus::Succeeded;
	}

	// 위 종료 조건에 안 걸리면 하나 더 담을 의욕이 있는지 확률적으로 판단
	// (FCSCond_WantsMoreItems에 있던 RollContinueShopping 판정이 여기로 옮겨옴)
	if (!Decision.RollContinueShopping(ItemsInBasket))
	{
		UE_LOG(LogTemp, Log, TEXT("[세션종료 확인용] %s | 사유=확률탈락 | 담은개수=%d/%d | 확률=%.2f"),
			*Customer.GetName(), ItemsInBasket, State.PlannedBasketTarget,
			Decision.ContinueShoppingProbability(ItemsInBasket));
		return EStateTreeRunStatus::Succeeded;
	}

	// 계속 쇼핑 -> 다음 라운드까지 SessionInterval만큼 대기 (ElapsedTime은 호출부에서 이미 0으로 리셋됨)
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FCSTask_BrowseShelf::Tick(FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.ElapsedTime += DeltaTime;

	ACSCustomerCharacter* Customer = Data.Actor;
	AActor* ShelfActor = Data.TargetShelf;
	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

	if (State == nullptr || Subsystem == nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Decision 조회를 여기로 옮김
	UCSPurchaseDecisionComponent* Decision = Customer->FindComponentByClass<UCSPurchaseDecisionComponent>();
	if (Decision == nullptr)
	{
		// 이 손님에게 판단 컴포넌트가 붙어있지 않으면 세션 자체를 진행할 수 없으므로 여기서 종료
		return EStateTreeRunStatus::Succeeded;
	}

	// 거절 즉시표시(또는 가격 반응 표시)가 떠 있는 동안은 다른 판단을 전부 건너뛰고 대기만 함
	// (손님이 다음 진열대로 넘어가지 않고 그 자리에 머무르게 하는 역할)
	// 대기시간은 방금 저장된 사유(State->LastRejectionInfo.Reason)에 따라 달라짐 - ShelfEmpty만 짧음
	if (Data.bAwaitingRejectionExit)
	{
		if (Data.ElapsedTime < Decision->GetInstantDisplayDuration(State->LastRejectionInfo.Reason))
		{
			return EStateTreeRunStatus::Running;
		}

		Data.bAwaitingRejectionExit = false;
		Data.ElapsedTime = 0.0f;

		// 대기가 실제 거절이 아니라 가격 반응은 있었지만 결국 사는 케이스였다면
		// 이제서야 상품을 담고 세션을 이어간다 (CS_HandlePriceReactionThenBuy가 걸어둔 플래그)
		if (Data.bPendingTakeProductAfterReaction)
		{
			Data.bPendingTakeProductAfterReaction = false;
			AActor* PendingProduct = Data.PendingProductActor;
			Data.PendingProductActor = nullptr;

			// 최대 몇 초씩 대기하는 동안 다른 손님이 같은 상품을 먼저 집어갔을 수 있음
			// 이 방어가 없으면 이미 사라진 액터를 TakeProduct에 넘겨 크래시로 이어짐
			if (IsValid(PendingProduct))
			{
				Subsystem->TakeProduct(ShelfActor, PendingProduct, Customer);
				Data.RoundCount++;
			}
			return CS_EvaluateSessionContinuation(Data, *Customer, *State, *Decision);
		}

		// 그 외에는 기존 그대로 - 실제 거절이었으므로 아무것도 안 담고 세션 종료
		return EStateTreeRunStatus::Succeeded;
	}

	// 첫 라운드(세션 진입)에 재고가 없으면 세션 자체를 시작하지 않고 GlanceTime만큼만 짧게 머물다 지나감
	// RoundCount == 0으로 제한하는 이유: 세션 도중(라운드 1 이상)에 다른 손님이 재고를 다 사가서 없어진 경우는
	// 여기로 다시 빠지지 않고, 아래 BuildPurchaseCandidates가 빈 배열을 만들어 DecidePurchase가 자연히
	// false(미구매)를 반환하며 세션이 끝나도록 둔다
	if (Data.RoundCount == 0 && !Subsystem->HasShelfStock(ShelfActor))
	{
		if (Data.ElapsedTime < Data.GlanceTime)
		{
			return EStateTreeRunStatus::Running;
		}

		State->MarkVisited(ShelfActor);
		// 진열대가 비어있어도 와서 확인하는 데는 시간이 들었다고 보고 최소 1만큼 차감 (기존 else 분기 그대로 유지)
		State->RemainingEvaluationBudget = FMath::Max(State->RemainingEvaluationBudget - 1, 0);

		// 빈 진열대도 거절 연출 대상 - 사유 기록 + 디버그 텍스트 + 방송 + 대기상태 진입
		FCSPurchaseRejectionInfo RejectionInfo;
		RejectionInfo.Reason = ECSPurchaseRejectReason::ShelfEmpty;
		CS_HandlePurchaseRejection(Data, *Customer, *State, *Subsystem, *Decision, RejectionInfo, Context.GetWorld());
		return EStateTreeRunStatus::Running;
	}

	// 라운드 0(첫 방문)은 BrowseTime, 그 이후 라운드는 SessionInterval만큼 대기 후 판단
	const float RequiredWait = (Data.RoundCount == 0) ? Data.BrowseTime : Data.SessionInterval;
	if (Data.ElapsedTime < RequiredWait)
	{
		return EStateTreeRunStatus::Running;
	}
	Data.ElapsedTime = 0.0f;

	// 사든 안 사든 방문한 건 사실이므로 첫 라운드에서 한 번만 기록
	// (재방문은 하지 않는다는 원칙 - HasUnvisitedShelf가 이 기록으로 게이트 역할을 함)
	if (Data.RoundCount == 0)
	{
		State->MarkVisited(ShelfActor);
	}

	// 진열대 위 상품들을 판단용 후보 목록으로 변환
	// 캐싱하지 않고 매 라운드 재조회 - 그 사이 다른 손님이 먼저 집어간 상품도 자연스럽게 반영됨
	const TArray<FCSPurchaseCandidate> Candidates = Subsystem->BuildPurchaseCandidates(ShelfActor);

	// 실제로 비교 대상에 오르는 개수는 상위 TopCandidateCount개로 잘리므로(ScoreAndSelectTopCandidates)
	// 봤다고 칠 개수도 진열대 상품 총개수가 아니라 그 상한을 넘지 않도록 함
	if (!Data.bBudgetCharged)
	{
		const int32 EvaluatedCount = FMath::Min(Candidates.Num(), Decision->GetEffectiveTopCandidateCount());
		State->RemainingEvaluationBudget = FMath::Max(State->RemainingEvaluationBudget - EvaluatedCount, 0);
		Data.bBudgetCharged = true;
	}

	// 후보 중 하나를 확률적으로 선택하거나 다 별로면 미구매(false) 반환
	FCSPurchaseCandidate Selected;
	FCSPurchaseRejectionInfo RejectionInfo;
	const bool bPicked = Decision->DecidePurchase(Candidates, Selected, RejectionInfo);

	UE_LOG(LogTemp, Log, TEXT("[카테고리 선호 파악용] %s | %s | %s"),
		*Customer->GetName(),
		Decision->Archetype ? *Decision->Archetype->DisplayName.ToString() : TEXT("종족없음"),
		bPicked ? *Selected.ProductRowHandle.RowName.ToString() : TEXT("미구매"));

	if (!bPicked)
	{
		// Reason==None: 반복 구매로 흥미가 떨어진 것뿐이라 플레이어에게 딱히 알릴 이유가 없는 조용한 미구매
		// 연출 없이 바로 세션 종료
		if (RejectionInfo.Reason == ECSPurchaseRejectReason::None)
		{
			return EStateTreeRunStatus::Succeeded;
		}

		// 그 외 사유는 거절 연출(사유 기록/디버그 텍스트/방송) 후 대기상태 진입
		CS_HandlePurchaseRejection(Data, *Customer, *State, *Subsystem, *Decision, RejectionInfo, Context.GetWorld());
		return EStateTreeRunStatus::Running;
	}

	// 가격 반응(Reason=PriceRejected, 무반응이면 None)이 있으면 즉시 담지 않고 먼저 멈춰서
	// 보여준 뒤(CS_HandlePurchaseRejection과 동일한 대기), 대기가 끝나면 그때 담는다
	// (위 bAwaitingRejectionExit 분기의 bPendingTakeProductAfterReaction 처리가 이어받음)
	if (RejectionInfo.Reason != ECSPurchaseRejectReason::None)
	{
		CS_HandlePriceReactionThenBuy(Data, *Customer, *State, *Decision, RejectionInfo, Selected.ProductActor, Context.GetWorld());
		return EStateTreeRunStatus::Running;
	}

	// 무반응이면 예전처럼 즉시 담고 바로 다음 판단
	Subsystem->TakeProduct(ShelfActor, Selected.ProductActor, Customer);
	Data.RoundCount++;
	return CS_EvaluateSessionContinuation(Data, *Customer, *State, *Decision);
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

	// 설 자리는 진열대의 CustomerShoppingPoint 마커에서 받아옵니다.
	FVector TargetLocation;
	FRotator TargetFacing;
	float TargetRadius = 0.0f;
	if (!Subsystem->TryGetShelfBrowsePoint(Data.TargetShelf, TargetLocation, TargetFacing, TargetRadius))
	{
		// 예외처리) 붙잡아두면 손님이 상품을 든 채 소멸해 재고가 증발하므로 그 자리에 떨굽니다.
		Subsystem->DropCustomerItems(Data.Actor);
		return EStateTreeRunStatus::Succeeded;
	}

	// 여기는 TargetShelf를 이미 알고 있으므로 진열대가 정한 영역 반경을 그대로 씁니다.
	const EPathFollowingRequestResult::Type Result =
		Data.AIController->MoveToLocation(TargetLocation, TargetRadius, false);

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
		return EStateTreeRunStatus::Failed;
	}

	// 진입 확정
	//  - 통계에서 행인과 구분되고
	//  - 매장 안 손님이 다시 가게 앞으로 나가지 않는다.
	State->MarkEnteredStore();

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
