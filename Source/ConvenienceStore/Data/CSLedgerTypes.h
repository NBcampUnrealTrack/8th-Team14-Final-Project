#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "CSLedgerTypes.generated.h"

class UTexture2D;

// 실제 상품 분류와 분리하여 상품에는 존재하지 않는 전체 보기 상태를 안전하게 표현합니다.
UENUM(BlueprintType)
enum class ECSLedgerCategoryFilter : uint8
{
	All,
	Alcohol,
	Beverage,
	FreshFood,
	FrozenFood,
	Potion,
	Scroll,
	Snack,
	Supply,
	Toiletry,
	Tool
};

// LockedProduct는 기존 Blueprint enum 직렬화 호환용으로만 보존합니다.
UENUM(BlueprintType)
enum class ECSLedgerDetailState : uint8
{
	NoSelection,
	UnlockedProduct,
	LockedProduct
};

// 여러 런타임 시스템의 값을 취합하여 WBP가 표현에만 집중하도록 전달하는 화면용 데이터입니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSLedgerProductViewData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	FDataTableRowHandle ProductRow;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	FName ProductId;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	FText ProductName;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	ECSProductCategory Category = ECSProductCategory::Beverage;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	TObjectPtr<UTexture2D> ProductIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	int32 CurrentStock = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	int32 MarketPrice = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	int32 PurchasePrice = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	int32 ShelfLifeDays = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	bool bPerishable = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger")
	int32 CurrentSalePrice = 0;

	// 기존 WBP 핀 호환용입니다. 모든 상품은 항상 해금된 상태로 표시합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	bool bUnlocked = true;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	int32 RequiredStoreLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	ECSReputationGrade RequiredReputationGrade = ECSReputationGrade::VeryBad;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	bool bStoreLevelRequirementMet = true;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	bool bReputationRequirementMet = true;
};
