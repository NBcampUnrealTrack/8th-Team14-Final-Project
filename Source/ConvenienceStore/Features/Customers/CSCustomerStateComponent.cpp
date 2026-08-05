// Fill out your copyright notice in the Description page of Project Settings.


#include "Features/Customers/CSCustomerStateComponent.h"

void UCSCustomerStateComponent::AddToSelectItem(const FName ItemId)
{
	if (ItemId != NAME_None)
	{
		// NAME_None이 아니면 선택한 아이템 배열에 추가
		SelectItemIds.Add(ItemId);
	}
}

bool UCSCustomerStateComponent::HasSelectedItems() const
{
	//	CartItemIds가 비어있지 않은지 bool로 반환
	if (SelectItemIds.Num() > 0)
	{
		return true;
	}
	else
	{
		return false;
	}
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
