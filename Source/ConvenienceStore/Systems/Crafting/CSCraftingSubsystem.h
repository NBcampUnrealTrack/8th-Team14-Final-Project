#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSCraftTypes.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSCraftingSubsystem.generated.h"

class UDataTable;
class ACSCraftStationActor;
class ACSProductPickupActor;
class UCSCraftFailureVisualRegistry;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnCraftIngredientAdded, int32, IngredientCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnManualCraftFinished, ECSCraftOutcome, Outcome);

// 레시피 표, 조합 판정, 한 판 동안의 레시피 발견 기록을 맡는다
// 판정에 난수를 쓰지 않는다. 손님 RNG 스트림 순서가 밀리지 않게 하려는 것
UCLASS()
class CONVENIENCESTORE_API UCSCraftingSubsystem : public UWorldSubsystem, public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	UCSCraftingSubsystem();
	UCSCraftFailureVisualRegistry* GetFailureVisualRegistry() const;

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// [백무송]
	// 불러오기 시 참가자 복원 순서를 반환하고 값이 작을수록 먼저 복원
	// GameState(0) → 서브시스템(5) → 월드 핸들러(10) 다음 순서로 제조를 복원
	virtual int32 GetSaveRestorePriority() const override { return 15; }

	// [백무송]
	// 제조 저장 데이터를 세이브의 제조 블록에 기록
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;

	// [백무송]
	// 세이브의 제조 블록으로 도감 기록·솥 재료·결과물·소문 시드를 복원
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	// 솥에 재료가 들어갈 때. 인수는 들어간 뒤 솥 안 재료 개수
	UPROPERTY(BlueprintAssignable, Category = "Crafting")
	FCSOnCraftIngredientAdded OnCraftIngredientAdded;

	// 직접 제조가 결과물을 내고 기록된 뒤. 자동 제조와 스폰 실패 재시도 대기는 오지 않는다
	UPROPERTY(BlueprintAssignable, Category = "Crafting")
	FCSOnManualCraftFinished OnManualCraftFinished;

	void NotifyIngredientAdded(int32 IngredientCount);

	// 튜토리얼이 제조대 클래스를 몰라도 되게 레벨의 첫 제조대에 대신 시킨다. 제조대가 없으면 아무것도 안 한다
	AActor* GetCraftStationActor() const;
	// 종류마다 1개씩 마법진 자리에 놓고 놓은 상품을 돌려준다
	TArray<ACSProductPickupActor*> PlaceProductsOnCraftCircle(const TArray<FName>& ProductIds);
	TArray<ACSProductPickupActor*> AddTagsToCraftResults(const TArray<FName>& NewTags);
	void ShowCraftStationMessage(FName MessageRowName, bool bWarning) const;
	void CloseCraftStationPanels();
	void SetCraftCloseupGuidesHidden(bool bHidden);
	// 진행 중이던 제조를 버리고 솥 재료와 결과 슬롯 물건을 지운다
	void ClearCraftStationPot();

	// 레시피 표나 상품 표가 없으면 false. 이때 제조대는 제조를 시작하지 않는다
	bool IsCraftingAvailable() const { return bCraftingAvailable; }

	// Ingredients는 재료 RowName별 개수. 레시피, 꽝, 개수, 불, 젓기, 거품 순으로 보고 먼저 틀린 것을 결과로 낸다
	FCSCraftEvaluation EvaluateCraft(
		const TMap<FName, int32>& Ingredients,
		ECSCraftHeat Heat,
		int32 StirCount,
		int32 PopHits) const;

	bool IsRecipeDiscovered(FName IngredientA, FName IngredientB) const;
	const TSet<FName>& GetDiscoveredRecipeRows() const { return DiscoveredRecipeRows; }

	// 도감 화면에 스크롤 없이 들어가는 양이고, 추리에 쓰이는 건 최근 시도라서 오래된 것은 버린다
	static constexpr int32 MaxRecentAttempts = 10;

	// 제조대에서 직접 만들었을 때만 부른다. 자동 제조는 발견, 소문, 시도 기록을 건드리지 않는다
	void RecordManualCraft(const FCSCraftAttemptRecord& Attempt);
	bool IsHeatRumorOpen(FName RecipeRowName) const { return HeatRumorRecipeRows.Contains(RecipeRowName); }
	bool IsStirRumorOpen(FName RecipeRowName) const { return StirRumorRecipeRows.Contains(RecipeRowName); }
	const TArray<FCSCraftAttemptRecord>& GetRecentAttempts() const { return RecentAttempts; }

	// 제조 전용 결과물이 아니면 false
	bool BuildLedgerInfo(FName ProductId, FCSCraftLedgerInfo& OutInfo) const;
	// 제조 전용 레시피의 재료 소문. 레시피 RowName 순
	TArray<FCSCraftRumorLine> GetMaterialRumorLines() const;
	// 표에 적힌 순서 그대로
	TArray<FName> GetRecipeRowNames(ECSCraftRecipeKind Kind) const;
	UDataTable* GetRumorTable() const { return RumorTable; }
	FText GetProductDisplayName(FName ProductId) const;

	FCSCraftSaveData CaptureCraftSaveData() const;
	// [백무송님] 저장 연결: 월드 상품을 전부 지우는 단계(CSWorldStateSaveHandler.cpp:333, :371-415)보다 뒤에 불러야 한다.
	// 먼저 부르면 되살린 솥 재료가 같이 지워진다. 월드 상태 핸들러의 복원 우선순위는 10이다
	void RestoreCraftSaveData(const FCSCraftSaveData& Data);

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
	UPROPERTY(EditDefaultsOnly, Category = "Crafting|Failure Visual")
	TSoftObjectPtr<UCSCraftFailureVisualRegistry> FailureVisualRegistry;

	static TPair<FName, FName> MakeIngredientKey(FName IngredientA, FName IngredientB);
	ACSCraftStationActor* FindCraftStation() const;
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

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> RumorTable;

	// [백무송님] 저장 연결: Transient라 지금은 저장되지 않는다. 불러오면 자동 제조 목록이 빈다.
	// 발견·소문·시도 기록은 CaptureCraftSaveData에 담겨 있다
	UPROPERTY(Transient)
	TSet<FName> DiscoveredRecipeRows;

	// 그 레시피에서 불을 맞힌 적이 있다
	UPROPERTY(Transient)
	TSet<FName> HeatRumorRecipeRows;

	// 그 레시피에서 젓기까지 맞힌 적이 있다
	UPROPERTY(Transient)
	TSet<FName> StirRumorRecipeRows;

	UPROPERTY(Transient)
	TArray<FCSCraftAttemptRecord> RecentAttempts;

	TMap<TPair<FName, FName>, FName> RecipeRowByIngredients;
	// 제조 전용 결과물 → 레시피 RowName. 장부가 상품으로 레시피를 찾는다
	TMap<FName, FName> CraftOnlyRecipeByResult;
	TSet<FName> JunkResultProducts;
	TSet<FName> CraftOnlyResultProducts;
	bool bCraftingAvailable = false;

	// 정보 창 갱신 때마다 같은 경고가 쌓이지 않게 한 번만 남긴다
	mutable TSet<FName> WarnedMissingMessages;
};
