// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSCustomerStateComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CONVENIENCESTORE_API UCSCustomerStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// 손님이 선택한 상품의 ID를 모아둔 배열
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TArray<FName> SelectItemIds;

	// 이번 방문에서 이미 구경한 진열대 액터 목록 (또 다시 안가게 막을려고)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TArray<TObjectPtr<AActor>> VisitedShelves;

	// 계산이 완료됬는지 알려주는 상태 변수
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	bool bCheckoutDone = false;

	// 손님이 나갈 때 걸어갈 목표 좌표
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	FVector ExitLocation = FVector::ZeroVector;

	void AddToSelectItem(const FName ItemId);
	bool HasSelectedItems() const;

	void MarkVisited(AActor* ShelfActor);
	bool HasVisited(const AActor* ShelfActor) const;

};
