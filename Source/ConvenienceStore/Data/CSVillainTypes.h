// 빌런 기능에서 쓰는 공용 타입.
//
// 빌런 생성 확률은 UCSCustomerArchetypeDataAsset, 빌런인지 아닌지는 UCSCustomerStateComponent

#pragma once

#include "CoreMinimal.h"
#include "CSVillainTypes.generated.h"

// 손님이 저지를 수 있는 빌런 행동의 종류.
// 더 추가 가능
UENUM(BlueprintType)
enum class ECSVillainAction : uint8
{
	None			UMETA(DisplayName = "없음(일반 손님)"),

	// 계산대를 거치지 않고 상품을 든 채 퇴장
	Shoplift		UMETA(DisplayName = "절도"),

};

// 종족(연령대)별로 빌런 행동 할 확률
USTRUCT(BlueprintType)
struct FCSVillainActionWeight
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Villain")
	ECSVillainAction Action = ECSVillainAction::None;

	// 이 종족의 빌런 중 이 행동이 뽑힐 상대 가중치.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Villain",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Weight = 1.0f;
};

// 도난당한 상품 한 종류의 집계.
//
// UCSCustomerStateComponent::SelectProducts를 상품ID 기준으로 접은 결과물이며,
// UCSCustomerRuntimeSubsystem::GetStolenProductRecords가 만든다.
// FCSSelectProduct를 대체하는 것이 아니라, 정산·상품 분석에 넘길 최소 정보만 뽑아낸 전달용 타입.
//
// 별도 타입이 필요한 이유:
//  1) SelectProducts는 "액터 1개 = 원소 1개"라 Quantity 개념이 없다. 상품별 합계는 접어야 나온다
//  2) 손님이 파괴되면 ProductId로 상품명을 되찾을 방법이 없어, 방송 시점에 스냅샷을 떠야 한다
//  3) ProductClass / ProductRuntimeState는 상품 액터 복원용 데이터라 하루 종일 들고 있을 이유가 없다
//  4) ACSStoreGameState가 손님 모듈(CSCustomerStateComponent.h)에 의존하지 않게 한다
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSStolenProductRecord
{
	GENERATED_BODY()

	// DT_ProductMaster의 RowName이다.
	UPROPERTY(BlueprintReadOnly, Category = "Villain|Shoplift")
	FName ProductId;

	UPROPERTY(BlueprintReadOnly, Category = "Villain|Shoplift")
	FText DisplayName;

	// 같은 상품을 여러 개 훔쳤으면 여기에 합산된다.
	UPROPERTY(BlueprintReadOnly, Category = "Villain|Shoplift")
	int32 Quantity = 0;
};


