#include "Systems/Crafting/CSCraftingSubsystem.h"

#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Features/Crafting/CSCraftStationActor.h"
#include "HAL/IConsoleManager.h"
#include "Systems/Crafting/CSCraftRumorSubsystem.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"

namespace
{
	TArray<FName> SortedNames(const TSet<FName>& Names)
	{
		TArray<FName> Result = Names.Array();
		Result.Sort(FNameLexicalLess());
		return Result;
	}

	// 같으면 빈 문자열, 다르면 처음 다른 항목 이름
	FString FindSaveDataDifference(const FCSCraftSaveData& A, const FCSCraftSaveData& B)
	{
		if (A.DiscoveredRecipeRows != B.DiscoveredRecipeRows) return TEXT("발견 레시피");
		if (A.HeatRumorRecipeRows != B.HeatRumorRecipeRows) return TEXT("불 소문");
		if (A.StirRumorRecipeRows != B.StirRumorRecipeRows) return TEXT("젓기 소문");
		if (A.RecentAttempts.Num() != B.RecentAttempts.Num()) return TEXT("최근 시도 줄 수");
		for (int32 Index = 0; Index < A.RecentAttempts.Num(); ++Index)
		{
			if (!(A.RecentAttempts[Index] == B.RecentAttempts[Index]))
			{
				return FString::Printf(TEXT("최근 시도 %d번째 줄"), Index + 1);
			}
		}
		if (A.Stations.Num() != B.Stations.Num()) return TEXT("제조대 수");
		auto RowNames = [](const TArray<FCSLooseProductRecord>& Records)
		{
			TArray<FName> Names;
			for (const FCSLooseProductRecord& Record : Records)
			{
				Names.Add(Record.ProductRow.RowName);
			}
			return Names;
		};
		for (int32 Index = 0; Index < A.Stations.Num(); ++Index)
		{
			const FCSCraftStationSaveRecord& StationA = A.Stations[Index];
			const FCSCraftStationSaveRecord& StationB = B.Stations[Index];
			if (StationA.StationName != StationB.StationName) return TEXT("제조대 이름");
			if (RowNames(StationA.PotProducts) != RowNames(StationB.PotProducts))
			{
				return FString::Printf(TEXT("%s 솥 재료"), *StationA.StationName.ToString());
			}
			if (RowNames(StationA.ResultProducts) != RowNames(StationB.ResultProducts))
			{
				return FString::Printf(TEXT("%s 결과 슬롯"), *StationA.StationName.ToString());
			}
		}
		if (A.RumorBaseSeed != B.RumorBaseSeed) return TEXT("소문 시드");
		return FString();
	}
}

namespace
{
#if !UE_BUILD_SHIPPING
	FAutoConsoleCommandWithWorldAndArgs CmdCraftSaveTest(
		TEXT("cs.CraftSaveTest"),
		TEXT("제조 저장 데이터를 포장했다가 비우고 다시 풀어서 처음과 같은지 확인합니다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
			if (!IsValid(Crafting) || !Crafting->IsCraftingAvailable())
			{
				UE_LOG(LogTemp, Warning, TEXT("[제조 저장 테스트] 플레이 중인 게임에서 실행하세요."));
				return;
			}
			// 저장 메뉴도 클로즈업 중에는 열리지 않는다. 진행 중인 제조가 비워지면 테스트가 상태를 망친다
			for (TActorIterator<ACSCraftStationActor> It(World); It; ++It)
			{
				if (It->IsCloseupActive())
				{
					UE_LOG(LogTemp, Warning, TEXT("[제조 저장 테스트] 클로즈업 중에는 실행하지 않습니다."));
					return;
				}
			}

			const FCSCraftSaveData Before = Crafting->CaptureCraftSaveData();

			// 불러오기처럼 기록을 비우고 솥 물건 액터도 지운 뒤 되살린다
			FCSCraftSaveData Empty;
			Empty.RumorBaseSeed = Before.RumorBaseSeed;
			for (const FCSCraftStationSaveRecord& Station : Before.Stations)
			{
				FCSCraftStationSaveRecord& EmptyStation = Empty.Stations.AddDefaulted_GetRef();
				EmptyStation.StationName = Station.StationName;
			}
			Crafting->RestoreCraftSaveData(Empty);
			Crafting->RestoreCraftSaveData(Before);

			const FCSCraftSaveData After = Crafting->CaptureCraftSaveData();
			const FString Difference = FindSaveDataDifference(Before, After);
			if (Difference.IsEmpty())
			{
				UE_LOG(LogTemp, Display, TEXT("[제조 저장 테스트] 성공 (발견 %d, 불 소문 %d, 젓기 소문 %d, 최근 시도 %d, 제조대 %d)"),
					Before.DiscoveredRecipeRows.Num(), Before.HeatRumorRecipeRows.Num(), Before.StirRumorRecipeRows.Num(),
					Before.RecentAttempts.Num(), Before.Stations.Num());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[제조 저장 테스트] 실패: %s"), *Difference);
			}
		}));
#endif

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

#if !UE_BUILD_SHIPPING
// 소문이 제조 전용 힌트에서 조합 힌트로 넘어가는지 보려면 6종을 다 만들어야 해서 둔 디버그 스위치
FAutoConsoleCommandWithWorldAndArgs CmdCraftDiscoverAll(
	TEXT("cs.CraftDiscoverAll"),
	TEXT("제조 전용 레시피를 전부 성공한 것으로 기록합니다. 게시판 소문은 다음 영업일부터 바뀝니다."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
		if (!IsValid(Crafting) || !Crafting->IsCraftingAvailable())
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] 플레이 중인 게임에서 실행하세요."));
			return;
		}

		int32 Count = 0;
		for (const FName RecipeRow : Crafting->GetRecipeRowNames(ECSCraftRecipeKind::CraftOnly))
		{
			const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(RecipeRow);
			if (Recipe == nullptr)
			{
				continue;
			}
			FCSCraftAttemptRecord Attempt;
			Attempt.RecipeRowName = RecipeRow;
			Attempt.ResultProduct = Recipe->ResultProduct;
			Attempt.Outcome = ECSCraftOutcome::Success;
			Crafting->RecordManualCraft(Attempt);
			++Count;
		}
		UE_LOG(LogTemp, Display, TEXT("[제조] 제조 전용 레시피 %d개를 발견한 것으로 기록했습니다."), Count);
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
		UE_LOG(LogTemp, Warning, TEXT("[제조] ACSStoreGameMode가 아니라서 마법 제조를 끕니다."));
		return;
	}

	RecipeTable = GameMode->GetCraftRecipeTable();
	FailureTable = GameMode->GetCraftFailureTable();
	MessageTable = GameMode->GetCraftMessageTable();
	KeyHintTable = GameMode->GetCraftKeyHintTable();
	ProductTable = GameMode->GetProductMasterTable();
	RumorTable = GameMode->GetCraftRumorTable();

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
	DropIfWrongStruct(RumorTable, FCSCraftRumorRow::StaticStruct(), TEXT("CraftRumorTable"));

	if (!IsValid(RecipeTable) || RecipeTable->GetRowMap().Num() == 0 || !IsValid(ProductTable))
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] GameMode의 CraftRecipeTable / ProductMasterTable이 비어 있어 마법 제조를 끕니다."));
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
	CraftOnlyRecipeByResult.Reset();

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
			CraftOnlyRecipeByResult.Add(Row->ResultProduct, Pair.Key);
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

		// 도감이 재료·불·젓기 소문을 그대로 보여 준다. 빈칸이면 실패해도 단서가 안 나온다
		if (Row->RecipeKind == ECSCraftRecipeKind::CraftOnly &&
			(Row->RumorStage1Text.IsEmpty() || Row->RumorStage2Text.IsEmpty() || Row->RumorStage3Text.IsEmpty()))
		{
			++WarningCount;
			UE_LOG(LogTemp, Warning, TEXT("[제조 검증] %s의 소문 문구(재료·불·젓기)에 빈칸이 있습니다."), *Pair.Key.ToString());
		}
	}

	const UEnum* OutcomeEnum = StaticEnum<ECSCraftOutcome>();
	for (const ECSCraftOutcome Outcome : { ECSCraftOutcome::MismatchedCount, ECSCraftOutcome::Burnt,
		ECSCraftOutcome::Undermixed, ECSCraftOutcome::Unknown, ECSCraftOutcome::PopFailed })
	{
		const FCSCraftFailureRow* FailureRow = GetFailureRow(Outcome);
		if (FailureRow == nullptr)
		{
			++WarningCount;
			UE_LOG(LogTemp, Warning, TEXT("[제조 검증] DT_CraftFailure에 '%s' 행이 없습니다."),
				*OutcomeEnum->GetNameStringByValue(static_cast<int64>(Outcome)));
		}
		else if (FailureRow->Description.IsEmpty())
		{
			++WarningCount;
			UE_LOG(LogTemp, Warning, TEXT("[제조 검증] DT_CraftFailure '%s'의 설명이 비어 있습니다."),
				*OutcomeEnum->GetNameStringByValue(static_cast<int64>(Outcome)));
		}
	}

	if (!IsValid(RumorTable))
	{
		++WarningCount;
		UE_LOG(LogTemp, Warning, TEXT("[제조 검증] GameMode의 CraftRumorTable이 비어 있어 게시판 소문이 제조 소문만 나옵니다."));
	}
	else
	{
		TMap<ECSCraftRumorKind, int32> KindCounts;
		for (const TPair<FName, uint8*>& Pair : RumorTable->GetRowMap())
		{
			const FCSCraftRumorRow* Row = reinterpret_cast<const FCSCraftRumorRow*>(Pair.Value);
			if (Row == nullptr)
			{
				continue;
			}
			++KindCounts.FindOrAdd(Row->Kind);
			if (Row->Kind == ECSCraftRumorKind::WeatherVariant &&
				(Row->TargetProduct.IsNone() || ProductTable->FindRow<FCSProductDataRow>(Row->TargetProduct, TEXT(""), false) == nullptr))
			{
				++WarningCount;
				UE_LOG(LogTemp, Warning, TEXT("[제조 검증] 날씨 소문 %s의 대상 상품 '%s'가 DT_ProductMaster에 없습니다."),
					*Pair.Key.ToString(), *Row->TargetProduct.ToString());
			}
		}

		// 게시판 풀 크기와 날씨 고정 자리 수가 이 개수를 기준으로 맞춰져 있다
		const UEnum* KindEnum = StaticEnum<ECSCraftRumorKind>();
		// 조합은 결과물마다 3문장(제조 전용은 레시피 1단계 소문이 첫 문장이라 2문장), 예고는 시간대마다 1문장
		const TPair<ECSCraftRumorKind, int32> ExpectedCounts[] = {
			{ ECSCraftRumorKind::Combination, 33 }, { ECSCraftRumorKind::Property, 6 },
			{ ECSCraftRumorKind::BasicMethod, 1 }, { ECSCraftRumorKind::Chatter, 16 },
			{ ECSCraftRumorKind::WeatherVariant, 9 }, { ECSCraftRumorKind::NewsPreview, 3 },
			{ ECSCraftRumorKind::HolidayPreview, 12 } };
		for (const TPair<ECSCraftRumorKind, int32>& Expected : ExpectedCounts)
		{
			const int32 Actual = KindCounts.FindRef(Expected.Key);
			if (Actual != Expected.Value)
			{
				++WarningCount;
				UE_LOG(LogTemp, Warning, TEXT("[제조 검증] DT_CraftRumor의 %s 행이 %d개입니다. %d개여야 합니다."),
					*KindEnum->GetNameStringByValue(static_cast<int64>(Expected.Key)), Actual, Expected.Value);
			}
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

const FCSCraftRecipeRow* UCSCraftingSubsystem::FindRecipeByRowName(FName RecipeRowName) const
{
	return IsValid(RecipeTable) ? RecipeTable->FindRow<FCSCraftRecipeRow>(RecipeRowName, TEXT(""), false) : nullptr;
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

void UCSCraftingSubsystem::RecordManualCraft(const FCSCraftAttemptRecord& Attempt)
{
	// 쓰레기가 나온 건 조합을 알아낸 게 아니다
	const bool bMadeProduct = Attempt.Outcome == ECSCraftOutcome::GreatSuccess ||
		Attempt.Outcome == ECSCraftOutcome::Success ||
		Attempt.Outcome == ECSCraftOutcome::Junk;
	if (bMadeProduct && !Attempt.RecipeRowName.IsNone())
	{
		DiscoveredRecipeRows.Add(Attempt.RecipeRowName);
	}

	// 맞힌 단계의 소문만 연다. 틀린 단계를 바로 알려 주면 추리할 게 없어진다.
	// 판정이 개수, 불, 젓기, 거품 순이라 덜 섞인 반죽이면 불을, 김빠진 덩어리면 불과 젓기를 맞힌 것이다
	const FCSCraftRecipeRow* Recipe = FindRecipeByRowName(Attempt.RecipeRowName);
	if (Recipe != nullptr && Recipe->RecipeKind == ECSCraftRecipeKind::CraftOnly)
	{
		if (Attempt.Outcome == ECSCraftOutcome::Undermixed || Attempt.Outcome == ECSCraftOutcome::PopFailed)
		{
			HeatRumorRecipeRows.Add(Attempt.RecipeRowName);
		}
		if (Attempt.Outcome == ECSCraftOutcome::PopFailed)
		{
			StirRumorRecipeRows.Add(Attempt.RecipeRowName);
		}
	}

	RecentAttempts.Add(Attempt);
	if (RecentAttempts.Num() > MaxRecentAttempts)
	{
		RecentAttempts.RemoveAt(0, RecentAttempts.Num() - MaxRecentAttempts);
	}

	OnManualCraftFinished.Broadcast(Attempt.Outcome);
}

void UCSCraftingSubsystem::NotifyIngredientAdded(int32 IngredientCount)
{
	OnCraftIngredientAdded.Broadcast(IngredientCount);
}

ACSCraftStationActor* UCSCraftingSubsystem::FindCraftStation() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<ACSCraftStationActor> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AActor* UCSCraftingSubsystem::GetCraftStationActor() const
{
	return FindCraftStation();
}

TArray<ACSProductPickupActor*> UCSCraftingSubsystem::PlaceProductsOnCraftCircle(const TArray<FName>& ProductIds)
{
	ACSCraftStationActor* Station = FindCraftStation();
	if (!IsValid(Station))
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] 레벨에 제조대가 없어 마법진에 상품을 놓지 못했습니다."));
		return {};
	}
	return Station->PlaceFreshProductsOnCircle(ProductIds);
}

TArray<ACSProductPickupActor*> UCSCraftingSubsystem::AddTagsToCraftResults(const TArray<FName>& NewTags)
{
	ACSCraftStationActor* Station = FindCraftStation();
	return IsValid(Station) ? Station->AddTagsToResultProducts(NewTags) : TArray<ACSProductPickupActor*>();
}

void UCSCraftingSubsystem::ShowCraftStationMessage(FName MessageRowName, bool bWarning) const
{
	if (const ACSCraftStationActor* Station = FindCraftStation())
	{
		Station->ShowBanner(MessageRowName, bWarning);
	}
}

void UCSCraftingSubsystem::CloseCraftStationPanels()
{
	if (ACSCraftStationActor* Station = FindCraftStation())
	{
		Station->CloseAllPanelsAndCloseup();
	}
}

void UCSCraftingSubsystem::SetCraftCloseupGuidesHidden(bool bHidden)
{
	if (ACSCraftStationActor* Station = FindCraftStation())
	{
		Station->SetCloseupGuidesHidden(bHidden);
	}
}

void UCSCraftingSubsystem::ClearCraftStationPot()
{
	if (ACSCraftStationActor* Station = FindCraftStation())
	{
		FCSCraftStationSaveRecord EmptyRecord;
		EmptyRecord.StationName = Station->GetFName();
		Station->RestorePotRecord(EmptyRecord);
	}
}

FText UCSCraftingSubsystem::GetProductDisplayName(FName ProductId) const
{
	const FCSProductDataRow* ProductRow = IsValid(ProductTable)
		? ProductTable->FindRow<FCSProductDataRow>(ProductId, TEXT(""), false)
		: nullptr;
	return ProductRow != nullptr && !ProductRow->DisplayName.IsEmpty() ? ProductRow->DisplayName : FText::FromName(ProductId);
}

bool UCSCraftingSubsystem::BuildLedgerInfo(FName ProductId, FCSCraftLedgerInfo& OutInfo) const
{
	OutInfo = FCSCraftLedgerInfo();
	const FName* RecipeRowName = CraftOnlyRecipeByResult.Find(ProductId);
	const FCSCraftRecipeRow* Recipe = RecipeRowName ? FindRecipeByRowName(*RecipeRowName) : nullptr;
	if (Recipe == nullptr)
	{
		return false;
	}

	OutInfo.bCraftOnly = true;
	OutInfo.RecipeRowName = *RecipeRowName;
	OutInfo.bDiscovered = DiscoveredRecipeRows.Contains(*RecipeRowName);
	return true;
}

TArray<FCSCraftRumorLine> UCSCraftingSubsystem::GetMaterialRumorLines() const
{
	TArray<FCSCraftRumorLine> Lines;
	for (const FName RowName : GetRecipeRowNames(ECSCraftRecipeKind::CraftOnly))
	{
		const FCSCraftRecipeRow* Recipe = FindRecipeByRowName(RowName);
		if (Recipe != nullptr && !Recipe->RumorStage1Text.IsEmpty())
		{
			Lines.Add({ RowName, Recipe->RumorStage1Text });
		}
	}
	Lines.Sort([](const FCSCraftRumorLine& A, const FCSCraftRumorLine& B) { return A.Key.LexicalLess(B.Key); });
	return Lines;
}

TArray<FName> UCSCraftingSubsystem::GetRecipeRowNames(ECSCraftRecipeKind Kind) const
{
	TArray<FName> RowNames;
	if (!IsValid(RecipeTable))
	{
		return RowNames;
	}
	for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
	{
		const FCSCraftRecipeRow* Row = reinterpret_cast<const FCSCraftRecipeRow*>(Pair.Value);
		// 재료 쌍이 겹쳐 판정에서 버려진 행은 도감에도 넣지 않는다
		const FName* UsedRow = Row ? RecipeRowByIngredients.Find(MakeIngredientKey(Row->IngredientA, Row->IngredientB)) : nullptr;
		if (Row != nullptr && Row->RecipeKind == Kind && UsedRow != nullptr && *UsedRow == Pair.Key)
		{
			RowNames.Add(Pair.Key);
		}
	}
	return RowNames;
}

FCSCraftSaveData UCSCraftingSubsystem::CaptureCraftSaveData() const
{
	FCSCraftSaveData Data;
	// 정렬해 두면 같은 기록은 늘 같은 배열이 되어 비교하기 쉽다
	Data.DiscoveredRecipeRows = SortedNames(DiscoveredRecipeRows);
	Data.HeatRumorRecipeRows = SortedNames(HeatRumorRecipeRows);
	Data.StirRumorRecipeRows = SortedNames(StirRumorRecipeRows);
	Data.RecentAttempts = RecentAttempts;

	UWorld* World = GetWorld();
	if (World != nullptr)
	{
		for (TActorIterator<ACSCraftStationActor> It(World); It; ++It)
		{
			Data.Stations.Add(It->CapturePotRecord());
		}
		Data.Stations.Sort([](const FCSCraftStationSaveRecord& A, const FCSCraftStationSaveRecord& B)
		{
			return A.StationName.LexicalLess(B.StationName);
		});
		if (UCSCraftRumorSubsystem* Rumor = World->GetSubsystem<UCSCraftRumorSubsystem>())
		{
			Data.RumorBaseSeed = Rumor->GetRumorBaseSeed();
		}
	}
	return Data;
}

void UCSCraftingSubsystem::RestoreCraftSaveData(const FCSCraftSaveData& Data)
{
	DiscoveredRecipeRows = TSet<FName>(Data.DiscoveredRecipeRows);
	HeatRumorRecipeRows = TSet<FName>(Data.HeatRumorRecipeRows);
	StirRumorRecipeRows = TSet<FName>(Data.StirRumorRecipeRows);
	RecentAttempts = Data.RecentAttempts;
	if (RecentAttempts.Num() > MaxRecentAttempts)
	{
		RecentAttempts.RemoveAt(0, RecentAttempts.Num() - MaxRecentAttempts);
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TMap<FName, ACSCraftStationActor*> StationsByName;
	for (TActorIterator<ACSCraftStationActor> It(World); It; ++It)
	{
		StationsByName.Add(It->GetFName(), *It);
	}
	for (const FCSCraftStationSaveRecord& Record : Data.Stations)
	{
		ACSCraftStationActor** Station = StationsByName.Find(Record.StationName);
		if (Station == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] 저장된 제조대 %s를 레벨에서 찾지 못해 솥 재료 %d개, 결과물 %d개를 되살리지 않습니다."),
				*Record.StationName.ToString(), Record.PotProducts.Num(), Record.ResultProducts.Num());
			continue;
		}
		(*Station)->RestorePotRecord(Record);
	}

	if (UCSCraftRumorSubsystem* Rumor = World->GetSubsystem<UCSCraftRumorSubsystem>())
	{
		Rumor->SetRumorBaseSeed(Data.RumorBaseSeed);
	}
}

// [백무송]
void UCSCraftingSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	// 기존 제조 저장 데이터 수집 함수로 제조 블록을 기록
	OutSave.Craft = CaptureCraftSaveData();

	// 캡처한 제조 기록 개수를 로그로 기록
	UE_LOG(LogTemp, Log,
		TEXT("[CSSave] 제조 캡처: 발견 레시피 %d, 최근 시도 %d, 제조대 %d"),
		OutSave.Craft.DiscoveredRecipeRows.Num(),
		OutSave.Craft.RecentAttempts.Num(),
		OutSave.Craft.Stations.Num());
}

// [백무송]
void UCSCraftingSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	// 기존 제조 저장 데이터 복원 함수로 도감 기록·솥 재료·결과물·소문 시드를 복원
	RestoreCraftSaveData(InSave.Craft);

	// 월드 복원 이후 실행 순서를 확인하도록 복원한 제조 기록 개수를 로그로 기록
	UE_LOG(LogTemp, Log,
		TEXT("[CSSave] 제조 복원: 발견 레시피 %d, 최근 시도 %d, 제조대 %d"),
		InSave.Craft.DiscoveredRecipeRows.Num(),
		InSave.Craft.RecentAttempts.Num(),
		InSave.Craft.Stations.Num());
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
