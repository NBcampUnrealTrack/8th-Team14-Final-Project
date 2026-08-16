// 손님 AI용 StateTree 조건 모음.
// Root의 자식 상태 선택(Selector)에 사용:
//   Shopping  ← HasUnvisitedShelf + WantsMoreItems
//   Checkout  ← HasItemsInCart

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "Data/CSVillainTypes.h"
#include "CSCustomerStateTreeConditions.generated.h"

class ACSCustomerCharacter;

// =====================================================================
// Customer: Has Unvisited Shelf
// 손님이 아직 안 가본 진열대가 하나라도 남았는지 판정
// 이 조건이 false가 되는 순간이 Goal "쇼핑 끝" 신호가 됩니다. 별도 코드 없이 이 조건 하나로 Shopping 상태를 건너뛰고 다음 상태로 넘어가게 만듬
// =====================================================================

USTRUCT()
struct FCSCond_HasUnvisitedShelfInstanceData
{
	GENERATED_BODY()

	// 서브시스템은 진열대 목록만 인지하고 누가 뭘 봤는지는 모릅니다.
	// 개인의 방문 기록은 컴포넌트에 있으므로, 판정 대상 손님 Actor를 받아 개인 상태를 꺼내 쓰기 위함입니다.
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	//true면 결과 반전 (남은 진열대가 없다로 사용 가능)
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

USTRUCT(meta = (DisplayName = "Customer: Has Unvisited Shelf"))
struct FCSCond_HasUnvisitedShelf : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSCond_HasUnvisitedShelfInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

// =====================================================================
// Customer: Has Items In Cart
// 손님이 산 게 하나라도 있는지 판정 (State->HasSelectedItems() 호출)
// 아무것도 안 산 손님이 계산대 줄에 서는 걸 막기 위함입니다. 장바구니가 비어있으면(false) Checkout을 건너뛰고 Leave로 직행
// =====================================================================

USTRUCT()
struct FCSCond_HasItemsInCartInstanceData
{
	GENERATED_BODY()

	// 장바구니는 개인 데이터이므로 공유 상태 조회가 불필요하여 서브시스템을 거치지 않고 바로 Actor에서 확인
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

USTRUCT(meta = (DisplayName = "Customer: Has Items In Cart"))
struct FCSCond_HasItemsInCart : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSCond_HasItemsInCartInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

// =====================================================================
// Customer: Has Abandoned Checkout
// 이번 방문에서 계산을 포기했는지 판정.
// Checkout 상태에 Invert로 걸어서 포기한 손님이 다시 줄 서는 것을 막고, ReturnItems 포기했고 상품이 있는 손님만 보냄
// =====================================================================
USTRUCT()
struct FCSCond_HasAbandonedCheckoutInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

USTRUCT(meta = (DisplayName = "Customer: Has Abandoned Checkout"))
struct FCSCond_HasAbandonedCheckout : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSCond_HasAbandonedCheckoutInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

// Customer: Wants More Items
// 지금까지 담은 개수를 보고 하나 더 담을 의욕이 있는지 확률적으로 판정
// CSPurchaseDecisionComponent::ContinueShoppingProbability()를 그대로 사용
// HasUnvisitedShelf와 AND로 묶여서 안 가본 진열대가 있어도 이제 그만 사고 싶어지면 멈추는 역할을 함

USTRUCT()
struct FCSCond_WantsMoreItemsInstanceData
{
	GENERATED_BODY()

	// 장바구니 개수(개인 데이터)와 판단 컴포넌트 둘 다 Actor에서 꺼내 써야 하므로
	// Actor를 Context로 받음
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	// true면 결과 반전
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

USTRUCT(meta = (DisplayName = "Customer: Wants More Items"))
struct FCSCond_WantsMoreItems : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSCond_WantsMoreItemsInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

// =====================================================================
// Customer: Is Villain
// 스폰 시 확정된 빌런 행동이 무엇인지, 그리고 아직 실행 전인지를 판정.
// =====================================================================
USTRUCT()
struct FCSCond_IsVillainInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;

	// 검사할 행동 None으로 두면 빌런이기만 하면 참으로 동작합니다.
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ECSVillainAction ActionToTest = ECSVillainAction::None;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

USTRUCT(meta = (DisplayName = "Customer: Is Villain"))
struct FCSCond_IsVillain : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSCond_IsVillainInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

// =====================================================================
// Customer: Should Force Leave
// 영업시간 종료로 이 손님을  내보내야 하는지 판정.
//
// Root의 On Tick 전이 조건이자 ForceLeaveGate 상태의 진입 조건으로 함께 씁니다.
// 판정 근거는 전부 서브시스템에 있고 이 구조체는 물어보기만 합니다.
// =====================================================================
USTRUCT()
struct FCSCond_ShouldForceLeaveInstanceData
{
	GENERATED_BODY()

	// 매장 상태(서브시스템)와 개인 상태(플래그, 빌런 여부)를 둘 다 봐야 하므로
	// 판정 대상 손님 Actor를 받습니다.
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACSCustomerCharacter> Actor = nullptr;
};

USTRUCT(meta = (DisplayName = "Customer: Should Force Leave"))
struct FCSCond_ShouldForceLeave : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FCSCond_ShouldForceLeaveInstanceData;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
