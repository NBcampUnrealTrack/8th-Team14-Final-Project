#pragma once

#include "CoreMinimal.h"
#include "CSCheckoutTypes.generated.h"

// 계산대가 손님 주문을 표시하고 금액을 계산할 때 사용하는 상품 한 줄의 데이터입니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCheckoutLineItem
{
	GENERATED_BODY()

	// 상품 마스터 데이터 테이블의 RowName을 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	FName ProductId;

	// 계산대 UI에 표시할 상품 이름을 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	FText ItemName;

	// 손님이 해당 상품을 구매하려는 수량을 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 Quantity = 1;

	// 결제 시점에 적용할 상품 한 개의 판매가를 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 UnitSalePrice = 0;

	// 정산에서 원가와 이익을 계산하기 위한 상품 한 개의 매입가를 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 UnitPurchasePrice = 0;

	// 상품별 판매가와 수량을 곱한 금액을 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 LineTotalSalePrice = 0;
};

// 결제 완료 시점의 가격과 주문 구성을 보존하여 정산에 사용하는 거래 기록입니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCheckoutTransaction
{
	GENERATED_BODY()

	// 하루 안에서 거래를 구분하기 위한 순번입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 TransactionId = 0;

	// 거래가 발생한 영업일입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 BusinessDay = 1;

	// 거래가 확정된 게임 시간입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	float GameMinutes = 0.0f;

	// 결제 당시의 상품별 수량과 가격 정보를 저장합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	TArray<FCSCheckoutLineItem> LineItems;

	// 이번 거래의 총매출입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 TotalSalesGold = 0;

	// 이번 거래에 판매된 상품의 총매입가입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 TotalCostGold = 0;

	// 총매출에서 총매입가를 뺀 이번 거래의 이익입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Checkout")
	int32 TotalProfitGold = 0;
};
