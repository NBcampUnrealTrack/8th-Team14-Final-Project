#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSPurchaseDecisionTypes.generated.h"

// 손님이 살지 말지 고민하는 후보 상품 하나를 표현하는 구조체
USTRUCT(BlueprintType)
struct FCSPurchaseCandidate
{
	GENERATED_BODY()

	// 상품 마스터 데이터테이블(DT_FantasyProductMaster)의 행을 가리키는 핸들
	// 가격, 카테고리 등 상품 고유 정보는 전부 여기서 조회
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Purchase")
	FDataTableRowHandle ProductRowHandle;

	// 실제로 진열대 위에 놓여 있는 상품 액터
	// 손님이 이 후보를 선택하면 이 액터를 TakeProduct()에 넘겨 집게 됨
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Purchase")
	TObjectPtr<AActor> ProductActor = nullptr;

	// 이 후보에 매겨진 0~1 점수
	UPROPERTY(BlueprintReadOnly, Category = "Purchase")
	float Score = 0.f;
};
