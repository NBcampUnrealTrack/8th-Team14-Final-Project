#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSProductTypes.generated.h"

namespace CSProductTags
{
	// 잠깐 쓰고 지워질 상품. 포장하거나 저장하면 원본을 찾아 지울 수 없게 된다
	inline const FName TemporaryProduct(TEXT("TemporaryProduct"));
}

// 새 카테고리는 반드시 맨 끝에 추가할 것 - 중간에 끼우면 DT_ProductMaster 46행과
// 아키타입 6종에 이미 저장된 기존 카테고리 값이 전부 밀린다(uint8 값 기반 저장이라 순서가 곧 값)
// 카테고리를 추가하면 CSPurchaseDecisionComponent.cpp의 AllProductCategories[] 배열도 같이 고칠 것 -
// 안 고쳐도 컴파일 에러가 나지 않아서 빠뜨리기 쉽다
UENUM(BlueprintType)
enum class ECSProductCategory : uint8
{
	Alcohol,
	Beverage,
	FreshFood,
	FrozenFood,
	Potion,
	Scroll,
	Snack,
	Supply,
	Toiletry,
	Tool,
	HangoverCure
};

// 화면에 보이는 카테고리 이름은 전부 여기서 가져간다. 화면마다 따로 적으면 같은 카테고리가 다른 이름으로 보인다
inline FText CSGetProductCategoryDisplayName(ECSProductCategory Category)
{
	switch (Category)
	{
	case ECSProductCategory::Alcohol: return NSLOCTEXT("ProductCategory", "Alcohol", "주류");
	case ECSProductCategory::Beverage: return NSLOCTEXT("ProductCategory", "Beverage", "음료");
	case ECSProductCategory::FreshFood: return NSLOCTEXT("ProductCategory", "FreshFood", "신선식품");
	case ECSProductCategory::FrozenFood: return NSLOCTEXT("ProductCategory", "FrozenFood", "냉동식품");
	case ECSProductCategory::Potion: return NSLOCTEXT("ProductCategory", "Potion", "포션");
	case ECSProductCategory::Scroll: return NSLOCTEXT("ProductCategory", "Scroll", "두루마리");
	case ECSProductCategory::Snack: return NSLOCTEXT("ProductCategory", "Snack", "간식");
	case ECSProductCategory::Supply: return NSLOCTEXT("ProductCategory", "Supply", "보급품");
	case ECSProductCategory::Toiletry: return NSLOCTEXT("ProductCategory", "Toiletry", "위생용품");
	case ECSProductCategory::Tool: return NSLOCTEXT("ProductCategory", "Tool", "도구");
	case ECSProductCategory::HangoverCure: return NSLOCTEXT("ProductCategory", "HangoverCure", "숙취해소제");
	default: return FText::GetEmpty();
	}
}

UENUM(BlueprintType)
enum class ECSProductShelfType : uint8
{
	ChilledShelf,
	FreezerShelf,
	PotionShelf,
	ScrollRack,
	SnackShelf,
	ToolRack,
	UtilityShelf,
	None UMETA(DisplayName = "None")
};

UENUM(BlueprintType)
enum class ECSProductRarity : uint8
{
	Common,
	Uncommon,
	Rare,
	Legendary
};

UENUM(BlueprintType)
enum class ECSReputationGrade : uint8
{
	None = 0 UMETA(Hidden),
	VeryBad = 1,
	Bad = 2,
	Normal = 3,
	Good = 4,
	Excellent = 5
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSProductRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Product|Runtime")
	bool bExpirationInitialized = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Product|Runtime")
	int32 ExpirationBusinessDay = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Product|Runtime|Freshness")
	bool bFreshnessInitialized = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Product|Runtime|Freshness")
	int32 Freshness = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Product|Runtime|Freshness")
	bool bFreshnessDecayActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Product|Runtime|Freshness")
	float FreshnessDecayStartTimeSeconds = 0.0f;

	void InitializeExpiration(
		int32 CurrentBusinessDay,
		bool bPerishable,
		int32 ShelfLifeDays)
	{
		if (bExpirationInitialized)
		{
			return;
		}

		bExpirationInitialized = true;
		ExpirationBusinessDay = 0;

		if (!bPerishable || ShelfLifeDays <= 0)
		{
			return;
		}

		const int64 CalculatedExpirationDay =
			static_cast<int64>(FMath::Max(1, CurrentBusinessDay)) + ShelfLifeDays;
		ExpirationBusinessDay = static_cast<int32>(
			FMath::Min<int64>(CalculatedExpirationDay, MAX_int32));
	}

	bool IsExpired(int32 CurrentBusinessDay) const
	{
		return bExpirationInitialized &&
			ExpirationBusinessDay > 0 &&
			CurrentBusinessDay >= ExpirationBusinessDay;
	}

	void InitializeFreshness(
		int32 InitialFreshness,
		bool bStartDecay,
		float CurrentGameTimeSeconds)
	{
		if (bFreshnessInitialized)
		{
			return;
		}

		bFreshnessInitialized = true;
		Freshness = FMath::Clamp(InitialFreshness, 0, 100);
		bFreshnessDecayActive = false;
		FreshnessDecayStartTimeSeconds = 0.0f;

		if (bStartDecay && Freshness > 0)
		{
			StartFreshnessDecay(CurrentGameTimeSeconds);
		}
	}

	int32 GetCurrentFreshness(
		float CurrentGameTimeSeconds,
		float DecayIntervalSeconds,
		int32 DecayAmountPerInterval) const
	{
		if (!bFreshnessInitialized ||
			!bFreshnessDecayActive ||
			Freshness <= 0 ||
			DecayIntervalSeconds <= 0.0f ||
			DecayAmountPerInterval <= 0)
		{
			return FMath::Clamp(Freshness, 0, 100);
		}

		const double ElapsedSeconds = FMath::Max(
			0.0,
			static_cast<double>(CurrentGameTimeSeconds) -
				static_cast<double>(FreshnessDecayStartTimeSeconds));
		const int64 CompletedIntervals = FMath::FloorToInt64(
			ElapsedSeconds / static_cast<double>(DecayIntervalSeconds));
		const int64 IntervalsToReachZero = FMath::DivideAndRoundUp<int64>(
			static_cast<int64>(Freshness),
			static_cast<int64>(DecayAmountPerInterval));
		if (CompletedIntervals >= IntervalsToReachZero)
		{
			return 0;
		}

		const int64 TotalDecay = CompletedIntervals * DecayAmountPerInterval;
		return static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(Freshness) - TotalDecay,
			0,
			100));
	}

	void StartFreshnessDecay(float CurrentGameTimeSeconds)
	{
		if (!bFreshnessInitialized || bFreshnessDecayActive || Freshness <= 0)
		{
			return;
		}

		bFreshnessDecayActive = true;
		FreshnessDecayStartTimeSeconds = FMath::Max(0.0f, CurrentGameTimeSeconds);
	}

	void StopFreshnessDecay(
		float CurrentGameTimeSeconds,
		float DecayIntervalSeconds,
		int32 DecayAmountPerInterval)
	{
		if (!bFreshnessInitialized || !bFreshnessDecayActive)
		{
			return;
		}

		Freshness = GetCurrentFreshness(
			CurrentGameTimeSeconds,
			DecayIntervalSeconds,
			DecayAmountPerInterval);
		bFreshnessDecayActive = false;
		FreshnessDecayStartTimeSeconds = 0.0f;
	}

	bool HasFreshnessRemaining(
		float CurrentGameTimeSeconds,
		float DecayIntervalSeconds,
		int32 DecayAmountPerInterval) const
	{
		return bFreshnessInitialized &&
			GetCurrentFreshness(
				CurrentGameTimeSeconds,
				DecayIntervalSeconds,
				DecayAmountPerInterval) > 0;
	}
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSProductDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	ECSProductCategory Category = ECSProductCategory::Beverage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Price",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 PurchasePrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Price",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 RecommendedSalePrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Order",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 OrderPackQuantity = 1;

	// 기존 상품은 발주 가능. 가챠 전용 상품만 false로 지정합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Order")
	bool bOrderable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|ShelfLife",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 ShelfLifeDays = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|ShelfLife")
	bool bPerishable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Shelf")
	ECSProductShelfType ShelfType = ECSProductShelfType::ChilledShelf;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Demand",
		meta = (ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "100"))
	int32 BaseDemand = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	ECSProductRarity Rarity = ECSProductRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product")
	FText Description;

	// 기존 DT/CSV 및 Blueprint 핀 호환용입니다. 상품 해금/발주 판정에는 사용하지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Unlock",
		meta = (ClampMin = "1", ClampMax = "10", UIMin = "1", UIMax = "10"))
	int32 StoreLevelUnlockCondition = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product|Unlock")
	ECSReputationGrade ReputationUnlockCondition = ECSReputationGrade::VeryBad;
};
