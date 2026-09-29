#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Data/CSGachaDataTypes.h"
#include "CSGachaPool.generated.h"

// 기계마다 이 에셋을 지정하여 보상 목록과 이용 비용을 편집하실 수 있습니다.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSGachaPool : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha", meta = (ClampMin = "0", UIMin = "0")) int32 CostGold = 500;
	// 이 테이블의 활성 행만 추첨하며 설정된 가중치를 그대로 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UDataTable> GachaTable;
	// CSV의 ProductRow가 비어 있을 때 이 테이블에서 RewardId 행을 찾습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Products") TObjectPtr<UDataTable> ProductTable;
	// CSV의 ArtifactRow가 비어 있으면 이 테이블에서 RewardId 행을 찾습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Artifacts") TObjectPtr<UDataTable> ArtifactTable;
};
