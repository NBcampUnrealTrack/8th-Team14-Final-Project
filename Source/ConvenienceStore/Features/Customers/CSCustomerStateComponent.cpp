// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerStateComponent.h"

void UCSCustomerStateComponent::AddSelectProduct(
	const FDataTableRowHandle& InProductRow,
	TSubclassOf<AActor> InProductClass,
	const FCSProductRuntimeState& InProductRuntimeState,
	int32 InSalePriceAtPickup)
{
	// 행 정보가 없으면 나중에 상품을 되돌릴 수도, 가격을 조회할 수도 없으므로 기록하지 않는다.
	if (InProductRow.DataTable == nullptr ||
		InProductRow.RowName.IsNone() ||
		InSalePriceAtPickup < 0)
	{
		return;
	}

	FCSSelectProduct Selected;
	Selected.ProductRow = InProductRow;
	Selected.ProductClass = InProductClass;
	Selected.ProductRuntimeState = InProductRuntimeState;
	Selected.SalePriceAtPickup = InSalePriceAtPickup;
	SelectProducts.Add(Selected);
}

bool UCSCustomerStateComponent::HasSelectedItems() const
{
	// 고른 상품이 하나라도 있으면 true
	return SelectProducts.Num() > 0;
}

void UCSCustomerStateComponent::ClearSelectItems()
{
	SelectProducts.Reset();
}

TArray<FName> UCSCustomerStateComponent::GetSelectProductIds() const
{
	TArray<FName> Result;
	Result.Reserve(SelectProducts.Num());

	for (int32 i = 0; i < SelectProducts.Num(); i++)
	{
		Result.Add(SelectProducts[i].ProductRow.RowName);
	}

	return Result;
}

void UCSCustomerStateComponent::MarkVisited(AActor* ShelfActor)
{
	// 받아온 진열대가 nullptr이 아니고, 아직 방문한 적이 없다면 추가
	if (ShelfActor != nullptr && HasVisited(ShelfActor) == false)
	{
		VisitedShelves.Add(ShelfActor);
	}
}

bool UCSCustomerStateComponent::HasVisited(const AActor* ShelfActor) const
{
	// 저장된 모든 방문 기록 개수만큼 반복문을 돈다.
	for (int32 i = 0; i < VisitedShelves.Num(); i++)
	{
		// 목록에 있는 선반과 방문 했는지 확인할려는 진열대와 일치하는지 확인
		if (VisitedShelves[i] == ShelfActor)
		{
			return true;
		}
	}
	// 없으면 false를 반환
	return false;
}

bool UCSCustomerStateComponent::IsForcedToLeave() const
{
	return bForcedToLeave;
}

void UCSCustomerStateComponent::MarkForcedToLeave()
{
	bForcedToLeave = true;
}

bool UCSCustomerStateComponent::AssignVillainAction(ECSVillainAction NewAction)
{
	// 빌런 해제(None 대입)는 지원하지 X
	// 한번 악인은 영원한 악인
	if (NewAction == ECSVillainAction::None)
	{
		return false;
	}

	// 이미 저지른 손님은 다시 빌런이 되지 않게
	if (bVillainActionDone)
	{
		return false;
	}

	PlannedVillainAction = NewAction;
	return true;
}

bool UCSCustomerStateComponent::IsVillain() const
{
	return PlannedVillainAction != ECSVillainAction::None;
}

bool UCSCustomerStateComponent::HasPendingVillainAction() const
{
	// 빌런이고, 아직 저지르지 않았을 때만 참
	return IsVillain() && (bVillainActionDone == false);
}

void UCSCustomerStateComponent::MarkVillainActionDone()
{
	bVillainActionDone = true;
}

void UCSCustomerStateComponent::RecordSpending(int32 Amount)
{
	SpentGold += Amount;
}

int32 UCSCustomerStateComponent::GetRemainingSoftBudget() const
{
	return FMath::Max(SoftBudgetGold - SpentGold, 0);
}

int32 UCSCustomerStateComponent::GetRemainingHardWallet() const
{
	return FMath::Max(HardWalletLimitGold - SpentGold, 0);
}
