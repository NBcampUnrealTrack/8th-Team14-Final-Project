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


