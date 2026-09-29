#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/CSTutorialTypes.h"
#include "CSCraftTypes.generated.h"

class UStaticMesh;

// None은 불이 꺼진 상태라 레시피에는 쓰지 않는다
UENUM(BlueprintType)
enum class ECSCraftHeat : uint8
{
	None = 0 UMETA(Hidden),
	Low UMETA(DisplayName = "약불"),
	Medium UMETA(DisplayName = "중불"),
	High UMETA(DisplayName = "강불")
};

// 결과 종류에 따라 판정 항목과 발주·계획 상품 처리가 달라진다
UENUM(BlueprintType)
enum class ECSCraftRecipeKind : uint8
{
	None = 0 UMETA(Hidden),
	CraftOnly UMETA(DisplayName = "제조 전용"),
	ExistingProduct UMETA(DisplayName = "기존 상품"),
	Junk UMETA(DisplayName = "꽝")
};

// 실패 4종의 값 이름을 DT_CraftFailure RowName으로 쓴다. 이름을 바꾸면 DT 행 이름도 같이 바꿔야 한다
UENUM(BlueprintType)
enum class ECSCraftOutcome : uint8
{
	None = 0 UMETA(Hidden),
	GreatSuccess UMETA(DisplayName = "대성공"),
	Success UMETA(DisplayName = "성공"),
	Junk UMETA(DisplayName = "꽝"),
	MismatchedCount UMETA(DisplayName = "짝짝이 덩어리"),
	Burnt UMETA(DisplayName = "새까맣게 탄 덩어리"),
	Undermixed UMETA(DisplayName = "덜 섞인 반죽"),
	Unknown UMETA(DisplayName = "정체불명의 덩어리")
};

// 제조대 진행 단계. 순서가 고정이고 완성하면 Idle로 돌아간다
UENUM(BlueprintType)
enum class ECSCraftStep : uint8
{
	Idle UMETA(DisplayName = "대기"),
	Heat UMETA(DisplayName = "불"),
	Stir UMETA(DisplayName = "젓기"),
	Powder UMETA(DisplayName = "가루")
};

// 레시피에 붙지 않는 소문 분류. 게시판 소문 탭에서 쓴다
UENUM(BlueprintType)
enum class ECSCraftRumorKind : uint8
{
	None = 0 UMETA(Hidden),
	Combination UMETA(DisplayName = "조합"),
	Property UMETA(DisplayName = "성질"),
	BasicMethod UMETA(DisplayName = "기본 조리법"),
	Chatter UMETA(DisplayName = "잡담"),
	WeatherVariant UMETA(DisplayName = "날씨 변형")
};

// 재료·결과는 DT_ProductMaster RowName. 실제로 있는지는 제조 서브시스템 초기화 때 검사한다
// 재료 A·B는 순서를 따지지 않는다. 두 RowName을 정렬한 쌍으로 찾는다
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCraftRecipeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Ingredient")
	FName IngredientA;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Ingredient", meta = (ClampMin = "1", UIMin = "1"))
	int32 IngredientACount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Ingredient")
	FName IngredientB;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Ingredient", meta = (ClampMin = "1", UIMin = "1"))
	int32 IngredientBCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Result")
	ECSCraftRecipeKind RecipeKind = ECSCraftRecipeKind::CraftOnly;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Result")
	FName ResultProduct;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Method")
	ECSCraftHeat Heat = ECSCraftHeat::Medium;

	// 정확히 일치해야 맞는다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Method", meta = (ClampMin = "0", UIMin = "0"))
	int32 StirCount = 3;

	// 꽝은 판정에 안 쓰지만 젓기 단계 거품 연출이 이 주기를 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Method", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float BubblePeriodSeconds = 2.0f;

	// 도감 소문 1단계 재료, 2단계 불, 3단계 젓기. 제조 전용 행만 채운다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Rumor", meta = (MultiLine = "true"))
	FText RumorStage1Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Rumor", meta = (MultiLine = "true"))
	FText RumorStage2Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Rumor", meta = (MultiLine = "true"))
	FText RumorStage3Text;
};

// RowName은 ECSCraftOutcome 값 이름(MismatchedCount, Burnt, Undermixed, Unknown)
// 쓰레기는 DT_ProductMaster에 행이 없어서 이름과 메시를 여기 둔다
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCraftFailureRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Failure")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Failure", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Failure")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

// 제조 안내 문구. 문구를 코드나 WBP에 직접 쓰지 않으려고 RowName으로 찾는다
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCraftMessageRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Message", meta = (MultiLine = "true"))
	FText Text;
};

// 클로즈업 하단 키 안내. 튜토리얼 키 힌트 구조를 그대로 쓴다
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCraftKeyHintRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|KeyHint")
	ECSCraftStep Step = ECSCraftStep::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|KeyHint")
	int32 SortOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|KeyHint")
	FCSTutorialHintEntry Hint;
};

// 레시피에 붙지 않는 소문(조합, 성질, 기본 조리법, 잡담, 날씨 변형)
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCraftRumorRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Rumor")
	ECSCraftRumorKind Kind = ECSCraftRumorKind::Chatter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Rumor", meta = (MultiLine = "true"))
	FText Text;

	// 날씨 변형일 때만 쓴다. 내일 날씨로 수요가 오르는 상품 RowName
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft|Rumor")
	FName TargetProduct;
};

// 실패하면 ResultProduct는 None, Quantity는 1(쓰레기 1개)
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSCraftEvaluation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	ECSCraftOutcome Outcome = ECSCraftOutcome::Unknown;

	// 재료 쌍에 맞는 레시피가 없으면 None
	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FName RecipeRowName;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FName ResultProduct;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	int32 Quantity = 1;
};
