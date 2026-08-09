// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CSCustomerStateComponent.generated.h"

class UDataTable;

// 손님이 고른 상품 하나의 정보.
// ProductId(RowName)만으로는 상품 액터를 다시 만들 수 없어서
// (어느 데이터 테이블인지, 어떤 BP였는지를 알 수 없음)
// 집는 시점에 원본 정보를 기록
USTRUCT(BlueprintType)
struct FCSSelectProduct
{
	GENERATED_BODY()

	// 상품의 데이터 테이블 행 (DataTable + RowName 쌍).
	// ProductRow.RowName이 ProductId다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	FDataTableRowHandle ProductRow;

	// 집었던 상품 액터의 클래스.
	// 같은 BP로 생성해야 DataAsset 기반 외형이 동일하게 적용된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TSubclassOf<AActor> ProductClass;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CONVENIENCESTORE_API UCSCustomerStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// 손님이 고른 상품 목록. 계산대 주문 생성과 포기 시 반환에 모두 사용된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TArray<FCSSelectProduct> SelectProducts;

	// 이번 방문에서 이미 구경한 진열대 액터 목록 (또 다시 안가게 막을려고)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	TArray<TObjectPtr<AActor>> VisitedShelves;

	// 계산이 완료됬는지 알려주는 상태 변수
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	bool bCheckoutDone = false;

	// 손님이 나갈 때 걸어갈 목표 좌표
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer")
	FVector ExitLocation = FVector::ZeroVector;

	// 예산 -------

	// 이번 방문에서 쓸 생각이었던 돈 (소프트 예산)
	// 실제 값을 뽑는 계산은 CSPurchaseDecisionComponent가 담당
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	int32 SoftBudgetGold = 0;

	// 물리적으로 낼 수 있는 최대 금액 (하드 지갑) 이걸 넘으면 무조건 구매 불가
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	int32 HardWalletLimitGold = 0;

	// 지금까지 실제로 쓴 금액 상품을 집을 때마다 늘어남
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	int32 SpentGold = 0;

	// false면 아직 소프트/하드 예산이 안 정해진 상태
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customer|Budget")
	bool bBudgetInitialized = false;

	// 상품을 실제로 구매(집기)했을 때 지출을 기록
	void RecordSpending(int32 Amount);

	// 남은 소프트 예산 (0 밑으로는 안 내려감)
	int32 GetRemainingSoftBudget() const;

	// 남은 하드 지갑 한도 (0 밑으로는 안 내려감)
	int32 GetRemainingHardWallet() const;

	// 고른 상품을 기록한다. 상품 액터를 파괴하기 전에 호출할 것.
	void AddSelectProduct(const FDataTableRowHandle& InProductRow, TSubclassOf<AActor> InProductClass);
	bool HasSelectedItems() const;

	// 고른 상품 목록을 비운다. 상품을 월드에 반환하거나 계산이 끝난 뒤 호출
	void ClearSelectItems();

	// 고른 상품의 ID(RowName) 목록. 계산대 주문 생성 등 Getter용
	UFUNCTION(BlueprintPure, Category = "Customer")
	TArray<FName> GetSelectProductIds() const;

	void MarkVisited(AActor* ShelfActor);
	bool HasVisited(const AActor* ShelfActor) const;

};
