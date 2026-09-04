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

// 오른쪽 페이지에서 동시에 하나만 보여야 하는 세 가지 화면 상태입니다.
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

	// 잠금 여부와 각 조건의 충족 여부를 함께 전달하여 WBP가 조건별 상태를 표시할 수 있게 합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	bool bUnlocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	int32 RequiredStoreLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	ECSReputationGrade RequiredReputationGrade = ECSReputationGrade::VeryBad;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	bool bStoreLevelRequirementMet = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ledger|Unlock")
	bool bReputationRequirementMet = false;
};
