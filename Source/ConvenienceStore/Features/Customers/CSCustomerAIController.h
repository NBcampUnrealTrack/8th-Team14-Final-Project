// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CSCustomerAIController.generated.h"

class UStateTreeAIComponent;
class ACSStreetRoute;
/**
 *
 */
UCLASS()
class CONVENIENCESTORE_API ACSCustomerAIController : public AAIController
{
	GENERATED_BODY()
public:
	ACSCustomerAIController();

	UStateTreeAIComponent* GetStateTreeAI() const;

	// 경로 점 배열을 순서대로 따라 이동하도록 길 따라가기에 요청
	// 엔진의 최단 경로 계산 없이 점을 그대로 사용하며, 마지막 점이 수락 반경 안이면 AlreadyAtGoal을 반환
	EPathFollowingRequestResult::Type MoveAlongPathPoints(
		const TArray<FVector>& PathPoints,
		float AcceptanceRadius);

	// 현재 위치의 레인을 유지하며 기준선을 따라 목적지까지 이동을 요청
	// 레인 경로 생성에 실패하면 Failed를 반환
	EPathFollowingRequestResult::Type MoveAlongStreetLane(
		const ACSStreetRoute& Route,
		const FVector& Destination,
		float AcceptanceRadius);

	// 현재 위치에서 레인 합류 지점까지의 NavMesh 경로와 합류 지점부터 목적지까지의 레인 경로를 이어 붙여 이동을 요청
	// 합류 지점에서 멈추지 않도록 한 번에 요청하며, 둘 중 하나라도 생성에 실패하면 Failed를 반환
	EPathFollowingRequestResult::Type MoveFromStoreToStreetLane(
		const ACSStreetRoute& Route,
		float LaneRatio,
		const FVector& Destination,
		float AcceptanceRadius);

protected:
	// StateTree를 실제로 실행하는 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;

private:
	// 이 컨트롤러의 NavMesh 기준으로 목표까지의 최단 경로 점을 조회
	// 경로를 찾지 못하거나 일부 경로만 찾으면 false를 반환
	bool FindNavPathPoints(
		const FVector& Goal,
		TArray<FVector>& OutPoints) const;

};
