#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSCraftTypes.h"
#include "CSCraftingSubsystem.generated.h"

class UDataTable;

// 레시피 표, 조합 판정, 한 판 동안의 레시피 발견 기록을 맡는다
// 판정에 난수를 쓰지 않는다. 손님 RNG 스트림 순서가 밀리지 않게 하려는 것
UCLASS()
class CONVENIENCESTORE_API UCSCraftingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// 레시피 표나 상품 표가 없으면 false. 이때 제조대는 제조를 시작하지 않는다
	bool IsCraftingAvailable() const { return bCraftingAvailable; }

	// Ingredients는 재료 RowName별 개수. 레시피, 꽝, 개수, 불, 젓기, 거품 순으로 보고 먼저 틀린 것을 결과로 낸다
	FCSCraftEvaluation EvaluateCraft(
		const TMap<FName, int32>& Ingredients,
		ECSCraftHeat Heat,
		int32 StirCount,
		int32 PopHits) const;

	bool IsRecipeDiscovered(FName IngredientA, FName IngredientB) const;
	// 성공, 대성공, 꽝일 때만 부른다. 쓰레기가 나온 건 조합을 알아낸 게 아니다
	void MarkRecipeDiscovered(FName RecipeRowName);
	const TSet<FName>& GetDiscoveredRecipeRows() const { return DiscoveredRecipeRows; }

	bool IsJunkResultProduct(FName ProductId) const;
	bool IsCraftOnlyResultProduct(FName ProductId) const;

	const FCSCraftRecipeRow* FindRecipe(FName IngredientA, FName IngredientB, FName* OutRecipeRowName = nullptr) const;
	const FCSCraftRecipeRow* FindRecipeByRowName(FName RecipeRowName) const;
	const FCSCraftFailureRow* GetFailureRow(ECSCraftOutcome Outcome) const;
	// 행이 없으면 RowName을 그대로 보여 준다. 빈칸보다 어느 행이 빠졌는지 찾기 쉽다
	FText GetMessageText(FName MessageRowName) const;
	TArray<FCSTutorialHintEntry> GetKeyHints(ECSCraftStep Step) const;
	// 레시피가 없으면 0
	float GetRecipePeriod(FName IngredientA, FName IngredientB) const;
	// 레시피가 없으면 0. 제조대가 자기 기본값을 쓴다
	int32 GetRecipePopCount(FName IngredientA, FName IngredientB) const;
	UDataTable* GetProductTable() const { return ProductTable; }

private:
	static TPair<FName, FName> MakeIngredientKey(FName IngredientA, FName IngredientB);
	void BuildRecipeMap();
	void ValidateTables() const;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> RecipeTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> FailureTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> MessageTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> KeyHintTable;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ProductTable;

	// 저장하지 않아서 한 판 동안만 유지된다
	UPROPERTY(Transient)
	TSet<FName> DiscoveredRecipeRows;

	TMap<TPair<FName, FName>, FName> RecipeRowByIngredients;
	TSet<FName> JunkResultProducts;
	TSet<FName> CraftOnlyResultProducts;
	bool bCraftingAvailable = false;

	// 정보 창 갱신 때마다 같은 경고가 쌓이지 않게 한 번만 남긴다
	mutable TSet<FName> WarnedMissingMessages;
};
