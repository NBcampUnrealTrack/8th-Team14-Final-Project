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

	// 선택된 진열대 (자식 상태의 Browse가 Input으로 사용)
	UPROPERTY(EditAnywhere, Category = "Output")
	TObjectPtr<AActor> TargetShelf = nullptr;

	// 선택된 진열대의 CustomerShoppingPoint 마커에서 받아온 설 자리
	// (자식 상태의 MoveTo가 Input으로 사용).
	// 거리 상수(BrowseDistance)로 계산하지 않고 진열대가 소유한 마커 값을 그대로 씁니다.
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

	// 진열대로 가는 이동이면 그 진열대를 바인딩합니다(PickNextShelf의 TargetShelf Output).
	// 지정하면 정지 반경과 도착 판정을 **진열대가 소유한 BrowseZoneRadius**로 처리합니다.
	// 계산대·출구처럼 영역이 없는 이동은 비워두면 아래 AcceptanceRadius로 기존 거리 판정을 씁니다.
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> TargetShelf = nullptr;

	// TargetShelf가 비었을 때만 쓰는 값입니다.
	// 경로 추종이 목표 앞에서 멈추는 거리이자, 도착 성공으로 인정하는 거리입니다.
	// (bStopOnOverlap을 꺼서 두 값이 같아졌습니다)
	//
	// AgentRadius(75) 아래로 내리지 마세요.
	// NavMesh가 벽·장애물에서 AgentRadius만큼 깎이므로, 목표가 구조물 근처면
	// 손님이 그보다 가까이 갈 수 없어 도착 판정이 영영 통과하지 못합니다.
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float AcceptanceRadius = 75.0f;
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
//
// 진열대 방문 1회 = "세션" 세션 안에서 여러 라운드에 걸쳐 반복 판단하도록 재작성됨
// (기존에는 방문 1회 = 판단 1회라서 최대 구매 개수가 진열대 개수에 종속되는 문제가 있었음)
// 재고가 없으면 GlanceTime만큼만 짧게 머물고 지나감. 재고가 있으면 BrowseTime 대기 후 첫 라운드 판단,
// 이후 계속 담을지 확률로 정해질 때마다 SessionInterval만큼 대기하고 라운드를 반복함
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

	// 첫 라운드에 진열대를 구경하는 시간(초)
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float BrowseTime = 1.5f;

	// 재고가 없는 진열대를 확인만 하고 지나가는 시간(초) - BrowseTime을 다 기다릴 필요가 없어 별도로 분리
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float GlanceTime = 0.4f;

	// 세션 두 번째 라운드부터 하나 더 담을지 판단하기 전까지 대기하는 시간(초)
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float SessionInterval = 0.8f;

	// 한 세션에서 허용하는 최대 라운드 수. 무한 루프 방지용 순수 방어 코드
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "1"))
	int32 MaxSessionRounds = 8;

	// 내부용 경과 시간 (라운드가 바뀔 때마다 0으로 리셋됨)
	UPROPERTY()
	float ElapsedTime = 0.0f;

	// 이번 세션에서 진행한 라운드 수. 0부터 시작하며 EnterState에서 리셋됨
	UPROPERTY()
	int32 RoundCount = 0;

	// 이번 세션에서 평가 예산을 이미 차감했는지 - 세션 전체를 통틀어 딱 한 번만 깎아야 함
	UPROPERTY()
	bool bBudgetCharged = false;

	// 거절 즉시표시(디버그 텍스트)가 떠 있는 동안 손님을 이 자리에 붙잡아두기 위한 플래그
	// true인 동안은 Tick 맨 위 분기가 다른 로직을 전부 건너뛰고 InstantDisplayDuration 경과만 기다림
	UPROPERTY()
	bool bAwaitingRejectionExit = false;

	// 위 대기가 실제 거절이 아니라 가격에 반응은 있지만 결국 사는 케이스일 때만 true
	// 대기가 끝나는 시점에 이 플래그를 보고 그냥 지나갈지(false, 기존 거절 흐름) 상품을 담을지(true) 갈림
	UPROPERTY()
	bool bPendingTakeProductAfterReaction = false;

	// 위 플래그가 true일 때 대기가 끝난 뒤 담을 상품 액터. 판단 시점(DecidePurchase 직후)과
	// 실제로 담는 시점(대기 종료 후) 사이에 몇 틱이 비므로 그 사이 기억해둘 곳이 필요해서 추가
	UPROPERTY()
	TObjectPtr<AActor> PendingProductActor = nullptr;
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
// SubduedDespawn : 제압된 빌런을 그 자리에 붙잡아 두고 시간이 지나면 파괴
//
// 상품 낙하와 방송은 피격 시점에 서브시스템이 이미 처리
// 이 Task는 "멈춤 + 시간 + 파괴" 세 가지뿐입니다.
// =====================================================================

USTRUCT()
struct FCSTask_SubduedDespawnInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	// 0보다 크면 이 값을, 0이면 서브시스템의 SubduedDespawnDelay를 사용합니다.
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float DespawnDelayOverride = 0.0f;

	// 내부용 경과 시간
	UPROPERTY()
	float ElapsedTime = 0.0f;
};

USTRUCT(meta = (DisplayName = "Customer: Subdued Despawn"))
struct FCSTask_SubduedDespawn : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	// 재선택 시 EnterState를 받지 않습니다.
	// 진입 조건인 IsSubdued는 한 번 켜지면 꺼지지 않으므로
	// Root의 On Tick 전이가 이미 Subdued에 있는데도 매 틱 다시 발동합니다.
	// 기본값(true)이면 그때마다 EnterState가 불려 ElapsedTime이 0으로 리셋되고,
	// 손님이 멈춘 채 영원히 사라지지 않음

	FCSTask_SubduedDespawn()
	{
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FCSTask_SubduedDespawnInstanceData;

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
