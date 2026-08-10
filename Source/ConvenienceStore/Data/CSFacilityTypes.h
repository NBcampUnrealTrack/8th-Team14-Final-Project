#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "CSFacilityTypes.generated.h"

// 시설의 게임플레이 역할을 구분합니다.
UENUM(BlueprintType)
enum class ECSFacilityType : uint8
{
	Shelf,
	Checkout,
	Delivery,
	Decoration
};

// DT_FantasyFacilityMaster의 한 행으로, 시설의 종류와 진열 환경을 정의합니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSFacilityDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility")
	FText FacilityName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility")
	ECSFacilityType FacilityType = ECSFacilityType::Shelf;

	// 이 시설이 제공하는 진열 환경입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Shelf")
	ECSProductShelfType ShelfType = ECSProductShelfType::UtilityShelf;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Purchase",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 PurchasePriceGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Ownership",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 InitialOwnedQuantity = 0;

	// CSV의 StoreLv_1 같은 값을 그대로 보존해 이후 해금 시스템에서 해석할 수 있게 합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Unlock")
	FName StoreLevelUnlockCondition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Unlock")
	ECSReputationGrade ReputationUnlockCondition = ECSReputationGrade::VeryBad;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Unlock")
	FName QuestUnlockCondition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|UI")
	int32 DisplayOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility")
	FText Description;
};
