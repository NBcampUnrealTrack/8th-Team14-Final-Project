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

			// 아직 아무것도 안 담았으면(첫 진열대 방문 전 상태) 무조건 계속 쇼핑하게 둠
			// (ContinueShoppingProbability는 1개 이상을 기준으로 감쇠가 시작)
			if (ItemsInBasket == 0)
			{
				bResult = true;
			}
			else
			{
				// 담은 개수를 바탕으로 하나 더 담을 확률을 구하고 그 확률로 동전 던지기
				const float ContinueProbability = Decision->ContinueShoppingProbability(ItemsInBasket);
				bResult = (FMath::FRand() < ContinueProbability);
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
