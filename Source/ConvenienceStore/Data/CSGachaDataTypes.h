#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "CSGachaDataTypes.generated.h"

UENUM(BlueprintType)
enum class ECSGachaRewardType : uint8
{
	Product,
	Artifact,
	// 기존 에셋을 읽기 위해 값은 유지하며, 엔딩 아이템은 추첨에서 제외합니다.
	Ending UMETA(Hidden)
};

// 보상 식별과 확률 데이터만 정의합니다. 추첨, 배출, 아이템 사용 로직은 포함하지 않습니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSGachaPoolRow : public FTableRowBase
{
	GENERATED_BODY()

	// Product: 기존 RowName, Artifact: Journal ArtifactId, Ending: DA_Ending ItemId.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
	FName RewardId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
	ECSGachaRewardType RewardType = ECSGachaRewardType::Product;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;
	// 신규 데이터는 이 행을 사용하며, 기존 CSV는 RewardId로 같은 행을 찾습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward",
		meta = (RowType = "/Script/ConvenienceStore.CSJournalArtifactDataRow"))
	FDataTableRowHandle ArtifactRow;

	// ProductVisualRegistry / JournalCatalog / EndingData를 RewardType에 맞게 참조합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
	TObjectPtr<UDataAsset> SourceCatalog = nullptr;

	// 기존 상품/엔딩 BP만 참조합니다. 미구현 기능 아이템은 비워 둡니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
	TSoftClassPtr<AActor> ActorClass;

	// Enabled 행의 Weight 합으로 정규화합니다. 초기 합 10000, Weight 50 = 0.5%.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward", meta = (ClampMin = "0"))
	int32 Weight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward")
	bool bEnabled = true;

	// 기획 설명만 보관하며 실제 효과/소환 클래스는 별도 담당자가 연결합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward", meta = (MultiLine = true))
	FText Description;
};
