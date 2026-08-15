#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataAsset.h"
#include "CSStoreProgressionDataAsset.generated.h"

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSReputationGradeThreshold
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Reputation")
	ECSReputationGrade Grade = ECSReputationGrade::VeryBad;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Reputation",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 MinimumReputationScore = 0;
};

UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSStoreProgressionDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UCSStoreProgressionDataAsset();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Experience",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 ExperiencePerSoldItem = 500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Experience",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 InitialStoreExperience = 0;

	// 배열 인덱스 + 1이 상점 레벨이며, 값은 해당 레벨의 누적 필요 경험치입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize,
		Category = "Store|Progression|Experience", meta = (ClampMin = "0", UIMin = "0"))
	TArray<int32> RequiredTotalExperience;

	// 배열 인덱스 + 1이 상점 레벨이며, 비어 있으면 기본 레벨 이름을 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Experience")
	TArray<FText> StoreLevelDisplayNames;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Reputation",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 MinimumReputationScore = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Reputation",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 InitialReputationScore = 6000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Progression|Reputation",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 MaximumReputationScore = 12499;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize,
		Category = "Store|Progression|Reputation")
	TArray<FCSReputationGradeThreshold> ReputationGradeThresholds;

	int32 GetMaximumStoreExperience() const;
	int32 GetStoreLevelForExperience(int32 StoreExperience) const;
	int32 GetRequiredTotalExperienceForLevel(int32 StoreLevel) const;
	FText GetStoreLevelDisplayName(int32 StoreLevel) const;
	ECSReputationGrade GetReputationGradeForScore(int32 ReputationScore) const;
};
