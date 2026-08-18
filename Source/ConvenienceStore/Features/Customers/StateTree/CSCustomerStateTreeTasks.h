// 손님 AI용 StateTree 태스크 모음.
//
// 설계 규칙:
// 1) 태스크는 얇게 - 실제 로직(판단, 재고, 대기열)은 UCSCustomerRuntimeSubsystem 경유.
// 2) 태스크 구조체의 멤버는 "트리 에셋을 쓰는 모든 손님이 공유"하므로
//    손님마다 달라지는 값은 반드시 FInstanceDataType(인스턴스 데이터)에 둔다.
// 3) 진열대는 AActor로만 다룬다. 실제 타입(ACSShelfActor)과 재고 조회는 서브시스템이 처리.
// 4) 에디터 바인딩용 카테고리 규칙:
//    Context   = 스키마의 AIController/Actor에 바인딩
//    Input     = 다른 태스크의 Output에 바인딩
//    Output    = 다른 태스크가 가져다 쓰는 값
//    Parameter = 에디터에서 직접 값 입력

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeExecutionTypes.h"
#include "CSCustomerStateTreeTasks.generated.h"

class AAIController;
class ACSCustomerCharacter;

// =====================================================================
//  PickNextShelf : 아직 안 본 진열대를 랜덤으로 골라 Output으로 내보냄
//  부모 상태(Shopping)에 붙여서 "데이터 홀더" 역할을 함
//  자식 상태(MoveToShelf, Browse)가 이 값을 읽을 수 있도록 상주
//  그래서 EnterState에서 Running을 반환하고 완료되지 않음
// =====================================================================

USTRUCT()
struct FCSTask_PickNextShelfInstanceData
{
	GENERATED_BODY()

	// 스키마의 Actor(손님 폰)에 바인딩
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	// 진열대 정면에서 얼마나 떨어져 설지.
	// 손님이 진열대 뒤에 선다면 음수로 바꿔서 방향 반전
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float BrowseDistance = 120.0f;

	// 선택된 진열대 (자식 상태의 Browse가 Input으로 사용)
	UPROPERTY(EditAnywhere, Category = "Output")
	TObjectPtr<AActor> TargetShelf = nullptr;

	// 선택된 진열대의 구경 위치 (자식 상태의 MoveTo가 Input으로 사용)
	UPROPERTY(EditAnywhere, Category = "Output")
	FVector TargetLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Customer: Pick Next Shelf"))
struct FCSTask_PickNextShelf : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_PickNextShelfInstanceData;

	FCSTask_PickNextShelf()
	{
		// 값만 들고 있으면 되므로 매 틱 호출될 필요가 없어 비활성화
		bShouldCallTick = false;
	}

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// =====================================================================
// MoveToLocation : NavMesh 기반 이동 (범용)
// 이동이 완료될 때까지 기다렸다가 성공 여부를 반환합니다.
// =====================================================================

USTRUCT()
struct FCSTask_MoveToLocationInstanceData
{
	GENERATED_BODY()

	// 이동 명령을 내릴 컨트롤러 바인딩
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	// 목표 좌표. 부모의 PickNextShelf Output 등과 바인딩(비어있으면 에러)
	UPROPERTY(EditAnywhere, Category = "Input")
	FVector TargetLocation = FVector::ZeroVector;

	/// 이 거리 안쪽이면 도착 처리
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float AcceptanceRadius = 50.0f;
};

USTRUCT(meta = (DisplayName = "Customer: Move To Location"))
struct FCSTask_MoveToLocation : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_MoveToLocationInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// =====================================================================
// BrowseShelf : 진열대 앞에서 구경 → 구매 판단 → 담기or패스
// 진열대 앞에서 BrowseTime만큼 머물다, 방문 기록을 남기고, 확률적으로 상품을 집습니다
// =====================================================================

USTRUCT()
struct FCSTask_BrowseShelfInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	// 구경할 진열대 (PickNextShelf의 TargetShelf에서 Output 받아옴)
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> TargetShelf = nullptr;

	// 구경하는 시간(초)
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float BrowseTime = 1.5f;

	// 내부용 경과 시간
	UPROPERTY()
	float ElapsedTime = 0.0f;
};

USTRUCT(meta = (DisplayName = "Customer: Browse Shelf"))
struct FCSTask_BrowseShelf : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_BrowseShelfInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
};

// =====================================================================
//  WaitInQueue : 줄서기
// 대기열에 등록하고 자기 자리로 이동, 순번이 바뀌면 앞으로 이동, 계산 완료 시 성공
// =====================================================================

USTRUCT()
struct FCSTask_WaitInQueueInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	// 틱마다 불필요한 이동 명령 재발급을 막기 위해 마지막으로 이동한 순번 기록
	UPROPERTY()
	int32 CachedQueueIndex = -1;

	// 인내심 시간
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float PatienceOverride = 0.0f;

	// 줄에 선 뒤 경과 시간
	UPROPERTY()
	float ElapsedWaitTime = 0.0f;
};

USTRUCT(meta = (DisplayName = "Customer: Wait In Queue"))
struct FCSTask_WaitInQueue : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_WaitInQueueInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// =====================================================================
//  LeaveAndDespawn : 퇴장 지점으로 이동 후 파괴
// =====================================================================

USTRUCT()
struct FCSTask_LeaveAndDespawnInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	float AcceptanceRadius = 80.0f;
};

USTRUCT(meta = (DisplayName = "Customer: Leave And Despawn"))
struct FCSTask_LeaveAndDespawn : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_LeaveAndDespawnInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;
};

// =====================================================================
// ReturnProducts : 빈 슬롯이 있는 진열대로 가서 상품을 되돌려놓음
// =====================================================================

USTRUCT()
struct FCSTask_ReturnProductsInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	// 진열대 정면에서 얼마나 떨어져 설지. PickNextShelf와 같은 의미 (수정예정)
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float BrowseDistance = 120.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	float AcceptanceRadius = 60.0f;

	// 되돌려놓으러 가는 진열대
	UPROPERTY()
	TObjectPtr<AActor> TargetShelf = nullptr;
};

USTRUCT(meta = (DisplayName = "Customer: Return Products"))
struct FCSTask_ReturnProducts : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_ReturnProductsInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// =====================================================================
// Shoplift : 계산하지 않고 상품을 든 채 도주
// 실행 표시와 방송은 서브시스템이 처리하고, 이 태스크 상태만 변경 한 뒤 즉시 성공 (속도나 애니메이션 등등)
// 실제 퇴장 이동은 전이로 연결된 Leave 상태(LeaveAndDespawn)가 담당
// =====================================================================

USTRUCT()
struct FCSTask_ShopliftInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	// 도주 시 걷기 속도 올라가게 함
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float FleeWalkSpeed = 400.0f;
};

USTRUCT(meta = (DisplayName = "Customer: Shoplift"))
struct FCSTask_Shoplift : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_ShopliftInstanceData;

	FCSTask_Shoplift()
	{
		// EnterState에서 전부 끝나므로 매 틱 호출될 필요가 없음
		bShouldCallTick = false;
	}

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// =====================================================================
// PrepareForcedLeave : 영업 종료 강제 퇴장 직전 정리
//
// 줄에서 빠지고 미결제 상품을 바닥에 떨어뜨린 뒤 즉시 성공.
// 실제 퇴장 이동은 전이로 연결된 Leave 상태가 담당합니다.
//
// 이동 정지를 여기서 하지 않는 이유:
//   MoveToLocation / WaitInQueue / ReturnProducts 세 태스크가 전부
//   ExitState에서 StopMovement()를 부르고, StateTree는 새 상태의 EnterState보다
//   이전 상태의 ExitState를 먼저 실행하므로 여기 도달 시점엔 이미 멈춰 있습니다.
// =====================================================================

USTRUCT()
struct FCSTask_PrepareForcedLeaveInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;
};

USTRUCT(meta = (DisplayName = "Customer: Prepare Forced Leave"))
struct FCSTask_PrepareForcedLeave : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_PrepareForcedLeaveInstanceData;

	FCSTask_PrepareForcedLeave()
	{
		// EnterState에서 전부 끝나므로 매 틱 호출될 필요가 없음
		bShouldCallTick = false;
	}

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

// =====================================================================
// ApproachStorefront : 스폰 지점에서 가게 앞까지 걸어가 입장 여부를 판정
//
// [핵심] 가게 앞에 도착하는 순간 태스크가 끝
// StateTree는 조건이 바뀌었다고 상태를 자동으로 벗어나지 않으므로,
// 도착 = 판정 = 상태 종료를 한 지점에 모아야 개점에 반응할 수 있습니다.
//
// 스폰 시점이 아니라 도착 시점에 굴리는 이유:
//   스폰 시 확정하면 걸어오는 도중에 문이 열려도 이미 "통과"로 정해져 그냥 지나감
//   반대로 걸어오는 도중에 문이 닫히면 닫힌 가게에 들어가게 됩니다.
//
// 손님마다 스폰 시각이 달라 가게 앞 도착 시점이 자연히 어긋나므로,
// 개점 순간 전원이 동시에 몰리지 않고 한 명씩 순차적으로 들어옴
//
// 판정은 손님당 한 번, 지나친 뒤에는 문이 열려도 돌아오지 않음
// 성공(Succeeded) = 매장 진입 결정 → Root 재선택 → Shopping
// 실패(Failed)    = 그냥 지나감    → Leave → ExitPoint로 계속 걸어감
// =====================================================================

USTRUCT()
struct FCSTask_ApproachStorefrontInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	float AcceptanceRadius = 80.0f;
};

USTRUCT(meta = (DisplayName = "Customer: Approach Storefront"))
struct FCSTask_ApproachStorefront : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSTask_ApproachStorefrontInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
