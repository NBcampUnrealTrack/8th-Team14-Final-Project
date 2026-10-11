#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "CSFacilityTypes.generated.h"

class ACSUsableToolActor;

// 시설의 게임플레이 역할을 구분합니다.
UENUM(BlueprintType)
enum class ECSFacilityType : uint8
{
	Shelf,
	Checkout,
	Delivery,
	Decoration,
	Tool
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

	// 시설 발주창에서 판매하지만 설치 절차 없이 기존 도구로 배송할 때만 지정합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Delivery")
	TSubclassOf<ACSUsableToolActor> DeliveredToolClass;

	// 도구의 루트 메시 원본 크기에 맞춘 월드 배율입니다. 설치형 시설에는 적용하지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery")
	FVector DeliveredToolScale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Ownership",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 InitialOwnedQuantity = 0;

	// 기존 DT/CSV 및 Blueprint 핀 호환용입니다. 아래 세 조건은 런타임에서 무시합니다.
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
