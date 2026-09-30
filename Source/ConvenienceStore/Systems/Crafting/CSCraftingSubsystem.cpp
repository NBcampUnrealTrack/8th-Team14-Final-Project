#include "Systems/Crafting/CSCraftingSubsystem.h"

#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"

namespace
{
#if !UE_BUILD_SHIPPING
FAutoConsoleCommandWithWorldAndArgs CmdCraftTest(
	TEXT("cs.CraftTest"),
	TEXT("제조 판정만 실행해 결과를 로그로 남깁니다. cs.CraftTest <재료A> <개수A> <재료B> <개수B> <Low|Medium|High> <젓기> <맞힌 거품 수>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
		if (!Crafting)
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] 플레이 중인 게임에서 실행하세요."));
			return;
		}
		if (Args.Num() < 7)
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] 사용법: cs.CraftTest <재료A> <개수A> <재료B> <개수B> <Low|Medium|High> <젓기> <맞힌 거품 수>"));
			return;
		}

		const UEnum* HeatEnum = StaticEnum<ECSCraftHeat>();
		const int64 HeatValue = HeatEnum ? HeatEnum->GetValueByNameString(Args[4]) : INDEX_NONE;
		if (HeatValue == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] 알 수 없는 불 세기: %s"), *Args[4]);
			return;
		}

		TMap<FName, int32> Ingredients;
		Ingredients.FindOrAdd(FName(*Args[0])) += FCString::Atoi(*Args[1]);
		Ingredients.FindOrAdd(FName(*Args[2])) += FCString::Atoi(*Args[3]);

		const FCSCraftEvaluation Result = Crafting->EvaluateCraft(
			Ingredients,
			static_cast<ECSCraftHeat>(HeatValue),
			FCString::Atoi(*Args[5]),
			FCString::Atoi(*Args[6]));

		UE_LOG(LogTemp, Display, TEXT("[제조] 판정=%s 레시피=%s 결과=%s 개수=%d"),
			*StaticEnum<ECSCraftOutcome>()->GetNameStringByValue(static_cast<int64>(Result.Outcome)),
			*Result.RecipeRowName.ToString(),
			*Result.ResultProduct.ToString(),
			Result.Quantity);
	}));
#endif
}

bool UCSCraftingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCSCraftingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const ACSStoreGameMode* GameMode = InWorld.GetAuthGameMode<ACSStoreGameMode>();
	if (!IsValid(GameMode))
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] ===== ACSStoreGameMode가 아니라서 마법 제조를 끕니다 ====="));
		return;
	}

	RecipeTable = GameMode->GetCraftRecipeTable();
	FailureTable = GameMode->GetCraftFailureTable();
	MessageTable = GameMode->GetCraftMessageTable();
	KeyHintTable = GameMode->GetCraftKeyHintTable();
	ProductTable = GameMode->GetProductMasterTable();

	// 행 구조체가 다른 표를 그대로 쓰면 행을 잘못 읽는다
	auto DropIfWrongStruct = [](TObjectPtr<UDataTable>& Table, const UScriptStruct* Expected, const TCHAR* Label)
	{
		if (IsValid(Table) && Table->GetRowStruct() != Expected)
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] %s의 행 구조체가 %s가 아닙니다. 이 표는 쓰지 않습니다."), Label, *Expected->GetName());
			Table = nullptr;
		}
	};
	DropIfWrongStruct(RecipeTable, FCSCraftRecipeRow::StaticStruct(), TEXT("CraftRecipeTable"));
	DropIfWrongStruct(FailureTable, FCSCraftFailureRow::StaticStruct(), TEXT("CraftFailureTable"));
	DropIfWrongStruct(MessageTable, FCSCraftMessageRow::StaticStruct(), TEXT("CraftMessageTable"));
	DropIfWrongStruct(KeyHintTable, FCSCraftKeyHintRow::StaticStruct(), TEXT("CraftKeyHintTable"));
	DropIfWrongStruct(ProductTable, FCSProductDataRow::StaticStruct(), TEXT("ProductMasterTable"));

	if (!IsValid(RecipeTable) || RecipeTable->GetRowMap().Num() == 0 || !IsValid(ProductTable))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[제조] ===== GameMode의 CraftRecipeTable / ProductMasterTable이 비어 있어 마법 제조를 끕니다 ====="));
		return;
	}

	BuildRecipeMap();
	ValidateTables();
	bCraftingAvailable = true;
}

TPair<FName, FName> UCSCraftingSubsystem::MakeIngredientKey(FName IngredientA, FName IngredientB)
{
	return IngredientA.Compare(IngredientB) <= 0
		? TPair<FName, FName>(IngredientA, IngredientB)
		: TPair<FName, FName>(IngredientB, IngredientA);
}

void UCSCraftingSubsystem::BuildRecipeMap()
{
	RecipeRowByIngredients.Reset();
	JunkResultProducts.Reset();
	CraftOnlyResultProducts.Reset();

	int32 CraftOnlyCount = 0;
	int32 ExistingCount = 0;
	int32 JunkCount = 0;
	for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
	{
		const FCSCraftRecipeRow* Row = reinterpret_cast<const FCSCraftRecipeRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}

		const TPair<FName, FName> Key = MakeIngredientKey(Row->IngredientA, Row->IngredientB);
		if (const FName* Existing = RecipeRowByIngredients.Find(Key))
		{
			// 먼저 나온 행을 쓴다
			UE_LOG(LogTemp, Warning, TEXT("[제조] 재료 쌍 (%s, %s)이 두 행에 있습니다: %s, %s. %s만 씁니다."),
				*Key.Key.ToString(), *Key.Value.ToString(), *Existing->ToString(), *Pair.Key.ToString(), *Existing->ToString());
			continue;
		}
		RecipeRowByIngredients.Add(Key, Pair.Key);

		switch (Row->RecipeKind)
		{
		case ECSCraftRecipeKind::CraftOnly:
			++CraftOnlyCount;
			CraftOnlyResultProducts.Add(Row->ResultProduct);
			break;
		case ECSCraftRecipeKind::ExistingProduct:
			++ExistingCount;
			break;
		case ECSCraftRecipeKind::Junk:
			++JunkCount;
			JunkResultProducts.Add(Row->ResultProduct);
			break;
		default:
			UE_LOG(LogTemp, Warning, TEXT("[제조] %s의 RecipeKind가 비어 있습니다."), *Pair.Key.ToString());
			break;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[제조] 레시피 %d (%d/%d/%d)"),
		RecipeRowByIngredients.Num(), CraftOnlyCount, ExistingCount, JunkCount);
}

void UCSCraftingSubsystem::ValidateTables() const
{
	int32 WarningCount = 0;
	auto CheckProduct = [this, &WarningCount](FName RecipeRow, FName ProductId, const TCHAR* Field) -> const FCSProductDataRow*
	{
		const FCSProductDataRow* ProductRow = ProductTable->FindRow<FCSProductDataRow>(ProductId, TEXT(""), false);
		if (ProductRow == nullptr)
		{
			++WarningCount;
			UE_LOG(LogTemp, Warning, TEXT("[제조 검증] %s의 %s '%s'가 DT_ProductMaster에 없습니다."),
				*RecipeRow.ToString(), Field, *ProductId.ToString());
		}
		return ProductRow;
	};

	for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
	{
		const FCSCraftRecipeRow* Row = reinterpret_cast<const FCSCraftRecipeRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}

		CheckProduct(Pair.Key, Row->IngredientA, TEXT("재료A"));
		CheckProduct(Pair.Key, Row->IngredientB, TEXT("재료B"));
		const FCSProductDataRow* ResultRow = CheckProduct(Pair.Key, Row->ResultProduct, TEXT("결과"));

		// 발주로 살 수 있으면 제조할 이유가 없고, 꽝이 발주 목록에 뜨게 된다
		const bool bMustNotBeOrderable =
			Row->RecipeKind == ECSCraftRecipeKind::CraftOnly || Row->RecipeKind == ECSCraftRecipeKind::Junk;
		if (ResultRow != nullptr && bMustNotBeOrderable && ResultRow->bOrderable)
		{
			++WarningCount;
			UE_LOG(LogTemp, Warning, TEXT("[제조 검증] %s의 결과 '%s'는 제조 전용·꽝인데 bOrderable이 true입니다."),
				*Pair.Key.ToString(), *Row->ResultProduct.ToString());
		}

		// 기준이 PopCount보다 크면 전부 맞혀도 실패하고, 0이면 하나도 안 맞혀도 성공한다
		if (Row->RecipeKind != ECSCraftRecipeKind::Junk &&
			(Row->PopCount < 1 || Row->PopSuccessHits < 1 || Row->PopSuccessHits > Row->PopCount))
		{
			++WarningCount;
			UE_LOG(LogTemp, Warning, TEXT("[제조 검증] %s의 거품 수 %d, 성공 기준 %d가 맞지 않습니다."),
				*Pair.Key.ToString(), Row->PopCount, Row->PopSuccessHits);
		}
	}

	const UEnum* OutcomeEnum = StaticEnum<ECSCraftOutcome>();
	for (const ECSCraftOutcome Outcome : { ECSCraftOutcome::MismatchedCount, ECSCraftOutcome::Burnt,
		ECSCraftOutcome::Undermixed, ECSCraftOutcome::Unknown, ECSCraftOutcome::PopFailed })
	{
		if (GetFailureRow(Outcome) == nullptr)
		{
			++WarningCount;
			UE_LOG(LogTemp, Warning, TEXT("[제조 검증] DT_CraftFailure에 '%s' 행이 없습니다."),
				*OutcomeEnum->GetNameStringByValue(static_cast<int64>(Outcome)));
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[제조 검증] 경고 %d건"), WarningCount);
}

const FCSCraftRecipeRow* UCSCraftingSubsystem::FindRecipe(FName IngredientA, FName IngredientB, FName* OutRecipeRowName) const
{
	if (!IsValid(RecipeTable))
	{
		return nullptr;
	}

	const FName* RowName = RecipeRowByIngredients.Find(MakeIngredientKey(IngredientA, IngredientB));
	if (RowName == nullptr)
	{
		return nullptr;
	}
	if (OutRecipeRowName != nullptr)
	{
		*OutRecipeRowName = *RowName;
	}
	return RecipeTable->FindRow<FCSCraftRecipeRow>(*RowName, TEXT(""), false);
}

FCSCraftEvaluation UCSCraftingSubsystem::EvaluateCraft(
	const TMap<FName, int32>& Ingredients,
	ECSCraftHeat Heat,
	int32 StirCount,
	int32 PopHits) const
{
	FCSCraftEvaluation Result;
	Result.Outcome = ECSCraftOutcome::Unknown;
	Result.Quantity = 1;

	if (Ingredients.Num() != 2)
	{
		return Result;
	}

	TArray<FName> Names;
	Ingredients.GetKeys(Names);
	FName RecipeRowName;
	const FCSCraftRecipeRow* Recipe = FindRecipe(Names[0], Names[1], &RecipeRowName);
	if (Recipe == nullptr)
	{
		return Result;
	}
	Result.RecipeRowName = RecipeRowName;

	// 꽝은 개수·조리법을 따지지 않는다
	if (Recipe->RecipeKind == ECSCraftRecipeKind::Junk)
	{
		Result.Outcome = ECSCraftOutcome::Junk;
		Result.ResultProduct = Recipe->ResultProduct;
		return Result;
	}

	const int32* CountA = Ingredients.Find(Recipe->IngredientA);
	const int32* CountB = Ingredients.Find(Recipe->IngredientB);
	if (CountA == nullptr || CountB == nullptr ||
		*CountA != Recipe->IngredientACount || *CountB != Recipe->IngredientBCount)
	{
		Result.Outcome = ECSCraftOutcome::MismatchedCount;
		return Result;
	}
	if (Heat != Recipe->Heat)
	{
		Result.Outcome = ECSCraftOutcome::Burnt;
		return Result;
	}
	if (StirCount != Recipe->StirCount)
	{
		Result.Outcome = ECSCraftOutcome::Undermixed;
		return Result;
	}

	if (PopHits < Recipe->PopSuccessHits)
	{
		Result.Outcome = ECSCraftOutcome::PopFailed;
		return Result;
	}

	Result.ResultProduct = Recipe->ResultProduct;
	// 품질은 가격 대신 개수로 나타낸다. 양념 조합은 거품을 다 맞혀도 1개
	if (Recipe->RecipeKind == ECSCraftRecipeKind::CraftOnly && PopHits >= Recipe->PopCount)
	{
		Result.Outcome = ECSCraftOutcome::GreatSuccess;
		Result.Quantity = 2;
	}
	else
	{
		Result.Outcome = ECSCraftOutcome::Success;
	}
	return Result;
}

bool UCSCraftingSubsystem::IsRecipeDiscovered(FName IngredientA, FName IngredientB) const
{
	const FName* RowName = RecipeRowByIngredients.Find(MakeIngredientKey(IngredientA, IngredientB));
	return RowName != nullptr && DiscoveredRecipeRows.Contains(*RowName);
}

void UCSCraftingSubsystem::MarkRecipeDiscovered(FName RecipeRowName)
{
	if (!RecipeRowName.IsNone())
	{
		DiscoveredRecipeRows.Add(RecipeRowName);
	}
}

bool UCSCraftingSubsystem::IsJunkResultProduct(FName ProductId) const
{
	return JunkResultProducts.Contains(ProductId);
}

bool UCSCraftingSubsystem::IsCraftOnlyResultProduct(FName ProductId) const
{
	return CraftOnlyResultProducts.Contains(ProductId);
}

const FCSCraftFailureRow* UCSCraftingSubsystem::GetFailureRow(ECSCraftOutcome Outcome) const
{
	if (!IsValid(FailureTable))
	{
		return nullptr;
	}
	const FName RowName(*StaticEnum<ECSCraftOutcome>()->GetNameStringByValue(static_cast<int64>(Outcome)));
	return FailureTable->FindRow<FCSCraftFailureRow>(RowName, TEXT(""), false);
}

FText UCSCraftingSubsystem::GetMessageText(FName MessageRowName) const
{
	if (IsValid(MessageTable))
	{
		if (const FCSCraftMessageRow* Row = MessageTable->FindRow<FCSCraftMessageRow>(MessageRowName, TEXT(""), false))
		{
			return Row->Text;
		}
	}

	if (!WarnedMissingMessages.Contains(MessageRowName))
	{
		WarnedMissingMessages.Add(MessageRowName);
		UE_LOG(LogTemp, Warning, TEXT("[제조] DT_CraftMessage에 '%s' 행이 없습니다."), *MessageRowName.ToString());
	}
	return FText::FromName(MessageRowName);
}

TArray<FCSTutorialHintEntry> UCSCraftingSubsystem::GetKeyHints(ECSCraftStep Step) const
{
	TArray<const FCSCraftKeyHintRow*> Rows;
	if (IsValid(KeyHintTable))
	{
		for (const TPair<FName, uint8*>& Pair : KeyHintTable->GetRowMap())
		{
			const FCSCraftKeyHintRow* Row = reinterpret_cast<const FCSCraftKeyHintRow*>(Pair.Value);
			if (Row != nullptr && Row->Step == Step)
			{
				Rows.Add(Row);
			}
		}
	}
	Rows.StableSort([](const FCSCraftKeyHintRow& A, const FCSCraftKeyHintRow& B) { return A.SortOrder < B.SortOrder; });

	TArray<FCSTutorialHintEntry> Hints;
	Hints.Reserve(Rows.Num());
	for (const FCSCraftKeyHintRow* Row : Rows)
	{
		Hints.Add(Row->Hint);
	}
	return Hints;
}

float UCSCraftingSubsystem::GetRecipePeriod(FName IngredientA, FName IngredientB) const
{
	const FCSCraftRecipeRow* Recipe = FindRecipe(IngredientA, IngredientB);
	return Recipe != nullptr ? Recipe->BubblePeriodSeconds : 0.0f;
}

int32 UCSCraftingSubsystem::GetRecipePopCount(FName IngredientA, FName IngredientB) const
{
	const FCSCraftRecipeRow* Recipe = FindRecipe(IngredientA, IngredientB);
	return Recipe != nullptr ? Recipe->PopCount : 0;
}
