// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerAIController.h"
#include "Components/StateTreeAIComponent.h"
#include "Features/Streets/CSStreetRoute.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

ACSCustomerAIController::ACSCustomerAIController()
{
	// BP로 승격시켜서 Start Logic Automatically를 체크해서 폰에 Possess되는 즉시 트리가 돌게 한다.
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
}

UStateTreeAIComponent* ACSCustomerAIController::GetStateTreeAI() const
{
	return StateTreeAI;
}

// -----------------------------------------------------------------------
// 레인 경로 이동
// -----------------------------------------------------------------------

// 경로 점 배열을 순서대로 따라 이동하도록 길 따라가기에 요청
EPathFollowingRequestResult::Type ACSCustomerAIController::MoveAlongPathPoints(
	const TArray<FVector>& PathPoints,
	float AcceptanceRadius)
{
	const APawn* MyPawn = GetPawn();
	if (MyPawn == nullptr || PathPoints.Num() < 2)
	{
		return EPathFollowingRequestResult::Failed;
	}

	// 마지막 점이 이미 수락 반경 안이면 이동 요청을 생략
	const FVector Goal = PathPoints.Last();
	if (FVector::Dist2D(MyPawn->GetNavAgentLocation(), Goal) <= AcceptanceRadius)
	{
		return EPathFollowingRequestResult::AlreadyAtGoal;
	}

	FNavPathSharedPtr Path = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(PathPoints);

	// NavMesh가 갱신되어도 최단 경로로 다시 계산하지 않고 레인 경로를 유지
	Path->EnableRecalculationOnInvalidation(false);
	Path->SetIgnoreInvalidation(true);

	// 길 따라가기와 경로 필터가 참조할 NavMesh 데이터를 지정
	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		Path->SetNavigationDataUsed(
			NavSys->GetNavDataForProps(GetNavAgentPropertiesRef(), MyPawn->GetNavAgentLocation()));
	}

	FAIMoveRequest MoveRequest(Goal);
	MoveRequest.SetAcceptanceRadius(AcceptanceRadius);

	// 도착 판정에 캡슐 반경을 더하지 않아 몸이 큰 손님이 목적지 앞에서 일찍 멈추지 않도록 설정
	MoveRequest.SetReachTestIncludesAgentRadius(false);

	const FAIRequestID RequestId = RequestMove(MoveRequest, Path);
	if (!RequestId.IsValid())
	{
		return EPathFollowingRequestResult::Failed;
	}

	return EPathFollowingRequestResult::RequestSuccessful;
}

// 현재 위치의 레인을 유지하며 기준선을 따라 목적지까지 이동을 요청
EPathFollowingRequestResult::Type ACSCustomerAIController::MoveAlongStreetLane(
	const ACSStreetRoute& Route,
	const FVector& Destination,
	float AcceptanceRadius)
{
	const APawn* MyPawn = GetPawn();
	if (MyPawn == nullptr)
	{
		return EPathFollowingRequestResult::Failed;
	}

	// 현재 발 위치의 옆 거리를 그대로 레인으로 사용
	FCSStreetLaneRequest LaneRequest;
	LaneRequest.StartGround = MyPawn->GetNavAgentLocation();
	LaneRequest.EndGround = Destination;
	LaneRequest.BodyRadius = MyPawn->GetSimpleCollisionRadius();

	TArray<FVector> PathPoints;
	if (!Route.BuildLanePath(LaneRequest, PathPoints))
	{
		return EPathFollowingRequestResult::Failed;
	}

	return MoveAlongPathPoints(PathPoints, AcceptanceRadius);
}

// 레인 합류 지점까지의 NavMesh 경로와 합류 지점부터의 레인 경로를 이어 붙여 이동을 요청
EPathFollowingRequestResult::Type ACSCustomerAIController::MoveFromStoreToStreetLane(
	const ACSStreetRoute& Route,
	float LaneRatio,
	const FVector& Destination,
	float AcceptanceRadius)
{
	const APawn* MyPawn = GetPawn();
	if (MyPawn == nullptr)
	{
		return EPathFollowingRequestResult::Failed;
	}

	const float BodyRadius = MyPawn->GetSimpleCollisionRadius();

	// 기억한 레인 비율로 입구 앞 합류 지점을 계산
	FVector MergeLocation;
	if (!Route.GetMergeLocation(LaneRatio, BodyRadius, Destination, MergeLocation))
	{
		return EPathFollowingRequestResult::Failed;
	}

	// 매장 안에서 합류 지점까지는 NavMesh 최단 경로를 사용
	TArray<FVector> PathPoints;
	if (!FindNavPathPoints(MergeLocation, PathPoints))
	{
		return EPathFollowingRequestResult::Failed;
	}

	// 합류 지점의 옆 거리가 기억한 레인이므로 그 레인 그대로 목적지까지 경로를 생성
	FCSStreetLaneRequest LaneRequest;
	LaneRequest.StartGround = MergeLocation;
	LaneRequest.EndGround = Destination;
	LaneRequest.BodyRadius = BodyRadius;

	TArray<FVector> LanePoints;
	if (!Route.BuildLanePath(LaneRequest, LanePoints))
	{
		return EPathFollowingRequestResult::Failed;
	}

	// 레인 경로의 첫 점은 합류 지점과 같으므로 제외하고 이어 붙임
	PathPoints.Append(LanePoints.GetData() + 1, LanePoints.Num() - 1);

	return MoveAlongPathPoints(PathPoints, AcceptanceRadius);
}

// 이 컨트롤러의 NavMesh 기준으로 목표까지의 최단 경로 점을 조회
bool ACSCustomerAIController::FindNavPathPoints(
	const FVector& Goal,
	TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();

	// 일부 경로만 찾은 경우 합류 지점에 닿지 못하므로 실패로 처리
	FAIMoveRequest MoveRequest(Goal);
	MoveRequest.SetAllowPartialPath(false);

	FPathFindingQuery Query;
	if (!BuildPathfindingQuery(MoveRequest, Query))
	{
		return false;
	}

	FNavPathSharedPtr NavPath;
	FindPathForMoveRequest(MoveRequest, Query, NavPath);

	if (!NavPath.IsValid() || !NavPath->IsValid())
	{
		return false;
	}

	const TArray<FNavPathPoint>& NavPoints = NavPath->GetPathPoints();
	OutPoints.Reserve(NavPoints.Num());

	for (const FNavPathPoint& NavPoint : NavPoints)
	{
		OutPoints.Add(NavPoint.Location);
	}

	return true;
}
