#include "Features/Customers/StateTree/CSCustomerStateTreeConditions.h"
#include "StateTreeExecutionContext.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"

bool FCSCond_HasUnvisitedShelf::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bResult = false;

	if (Data.Actor != nullptr)
	{
		// 개인 상태(방문 기록)
		const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
		// 월드에서 서브시스템(전체 진열대 정보) 획득
		const UCSCustomerRuntimeSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

		if (State != nullptr && Subsystem != nullptr)
		{
			// 서브시스템에 개인의 방문 기록을 넘겨서 안 가본 진열대가 남았는지 확인
			bResult = Subsystem->HasUnvisitedShelf(State->VisitedShelves);
		}
	}
	// bInvert가 true면 결과를 뒤집어서 반환
	if (Data.bInvert)
	{
		return (bResult == false);
	}
	else
	{
		return bResult;
	}
}

bool FCSCond_HasItemsInCart::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bResult = false;

	if (Data.Actor != nullptr)
	{
		const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
		if (State != nullptr)
		{
			// 장바구니 정보는 개인 데이터이므로 서브시스템을 안 거치고 바로 판정
			bResult = State->HasSelectedItems();
		}
	}

	if (Data.bInvert)
	{
		return (bResult == false);
	}
	else
	{
		return bResult;
	}
}
