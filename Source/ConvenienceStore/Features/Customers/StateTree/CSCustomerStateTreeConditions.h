// 손님 AI용 StateTree 조건 모음.
// Root의 자식 상태 선택(Selector)에 사용:
//   Shopping  ← HasUnvisitedShelf
//   Checkout  ← HasItemsInCart

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "CSCustomerStateTreeConditions.generated.h"

class ACSCustomerCharacter;

// =====================================================================
// Customer: Has Unvisited Shelf
// 손님이 아직 안 가본 진열대가 하나라도 남았는지 판정
// 이 조건이 false가 되는 순간이 Goal "쇼핑 끝" 신호가 됩니다. 별도 코드 없이 이 조건 하나로 Shopping 상태를 건너뛰고 다음 상태로 넘어가게 만듭니다
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
// 아무것도 안 산 손님이 계산대 줄에 서는 걸 막기 위함입니다. 장바구니가 비어있으면(false) Checkout을 건너뛰고 Leave로 직행합니다.
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
