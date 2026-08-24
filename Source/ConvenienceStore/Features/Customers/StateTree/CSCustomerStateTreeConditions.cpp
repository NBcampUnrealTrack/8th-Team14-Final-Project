#include "Features/Customers/StateTree/CSCustomerStateTreeConditions.h"
#include "StateTreeExecutionContext.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Customers/CSPurchaseDecisionComponent.h"

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

bool FCSCond_HasAbandonedCheckout::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bResult = false;

	if (Data.Actor != nullptr)
	{
		const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
		if (State != nullptr)
		{
			bResult = State->bCheckoutAbandoned;
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

bool FCSCond_WantsMoreItems::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 판단 근거(Actor, 상태, 판단 컴포넌트)를 못 찾을 때는 true로 둬서
	// 기존 동작(HasUnvisitedShelf만으로 계속 진행)을 해치지 않는 걸 기본값으로
	bool bResult = true;

	if (Data.Actor != nullptr)
	{
		// 장바구니 개수(개인 데이터)는 상태 컴포넌트에서 조회
		const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
		// 확률 계산 로직은 판단 컴포넌트가 갖고 있으므로 여기서 찾아옴
		const UCSPurchaseDecisionComponent* Decision = Data.Actor->FindComponentByClass<UCSPurchaseDecisionComponent>();

		if (State != nullptr && Decision != nullptr)
		{
			const int32 ItemsInBasket = State->SelectProducts.Num();

			// 탐색 예산까지 다 썼으면 매장에 안 가본 진열대가 남아있어도 그만 구경하게 함
			if (ItemsInBasket == 0)
			{
				bResult = (State->RemainingEvaluationBudget > 0);
			}
			else
			{
				// 확률 계산 + 굴림을 컴포넌트 안에서 한 번에 처리
				// 전역 FMath::FRand() 대신 그 손님의 RandomStream을 써서 재현 가능해짐
				bResult = Decision->RollContinueShopping(ItemsInBasket);
			}
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

bool FCSCond_IsVillain::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bResult = false;

	if (Data.Actor != nullptr)
	{
		// 빌런 여부는 개인 데이터이므로 서브시스템을 안 거치고 바로 상태 컴포넌트에서 판정
		const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
		if (State != nullptr && State->HasPendingVillainAction())
		{
			if (Data.ActionToTest == ECSVillainAction::None)
			{
				// 행동 종류를 안 따지는 경우 (부모 상태에서 묶어 쓸 때)
				bResult = true;
			}
			else
			{
				bResult = (State->PlannedVillainAction == Data.ActionToTest);
			}
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

bool FCSCond_ShouldForceLeave::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bResult = false;

	if (Data.Actor != nullptr)
	{
		// 매장 단계 판정과 예외 처리를 전부 서브시스템이 갖고 있으므로 물어보기만 한다.
		const UCSCustomerRuntimeSubsystem* Subsystem =
			Context.GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();

		if (Subsystem != nullptr)
		{
			bResult = Subsystem->ShouldForceLeaveStore(Data.Actor);
		}
	}

	return bResult;
}

bool FCSCond_IsSubdued::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bResult = false;

	if (Data.Actor != nullptr)
	{
		// 제압 여부는 개인 데이터이므로 서브시스템을 안 거치고 바로 상태 컴포넌트에서 판정
		const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
		if (State != nullptr)
		{
			bResult = State->IsSubdued();
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

bool FCSCond_HasEnteredStore::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	bool bResult = false;

	if (Data.Actor != nullptr)
	{
		// 입장 여부는 개인 데이터이므로 서브시스템을 안 거치고 바로 상태 컴포넌트에서 판정
		const UCSCustomerStateComponent* State = Data.Actor->GetCustomerState();
		if (State != nullptr)
		{
			bResult = State->HasEnteredStore();
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
