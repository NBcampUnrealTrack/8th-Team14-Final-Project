#include "Systems/Crafting/CSCraftRumorSubsystem.h"

#include "Data/CSCraftTypes.h"
#include "Data/CSWeatherTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/Weather/CSWeatherSubsystem.h"

extern TAutoConsoleVariable<int32> CVarCS_FixedSeed;

namespace
{
	// 같은 시드를 쓰는 다른 굴림과 결과가 겹치지 않게 섞는 값
	constexpr uint32 RumorSeedSalt = 0x52554D52;
	constexpr float DefaultBusinessStartMinutes = 540.0f;
}

bool UCSCraftRumorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCSCraftRumorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (ACSStoreGameState* StoreGameState = InWorld.GetGameState<ACSStoreGameState>(); IsValid(StoreGameState))
	{
		GameState = StoreGameState;
		StoreGameState->OnGameTimeChanged.AddUniqueDynamic(this, &UCSCraftRumorSubsystem::HandleGameTimeChanged);
		StoreGameState->OnBusinessDayStarted.AddUniqueDynamic(this, &UCSCraftRumorSubsystem::HandleBusinessDayChanged);
		StoreGameState->OnBusinessDayRestored.AddUniqueDynamic(this, &UCSCraftRumorSubsystem::HandleBusinessDayChanged);
	}
	if (UCSWeatherSubsystem* WeatherSubsystem = InWorld.GetSubsystem<UCSWeatherSubsystem>(); IsValid(WeatherSubsystem))
	{
		Weather = WeatherSubsystem;
		WeatherSubsystem->OnWeatherForecastUpdated.AddUniqueDynamic(this, &UCSCraftRumorSubsystem::HandleWeatherForecastUpdated);
	}
}

void UCSCraftRumorSubsystem::Deinitialize()
{
	if (ACSStoreGameState* StoreGameState = GameState.Get())
	{
		StoreGameState->OnGameTimeChanged.RemoveDynamic(this, &UCSCraftRumorSubsystem::HandleGameTimeChanged);
		StoreGameState->OnBusinessDayStarted.RemoveDynamic(this, &UCSCraftRumorSubsystem::HandleBusinessDayChanged);
		StoreGameState->OnBusinessDayRestored.RemoveDynamic(this, &UCSCraftRumorSubsystem::HandleBusinessDayChanged);
	}
	if (UCSWeatherSubsystem* WeatherSubsystem = Weather.Get())
	{
		WeatherSubsystem->OnWeatherForecastUpdated.RemoveDynamic(this, &UCSCraftRumorSubsystem::HandleWeatherForecastUpdated);
	}
	GameState.Reset();
	Weather.Reset();
	Super::Deinitialize();
}

int32 UCSCraftRumorSubsystem::GetRumorBaseSeed()
{
	const int32 FixedSeed = CVarCS_FixedSeed.GetValueOnGameThread();
	if (FixedSeed >= 0)
	{
		return FixedSeed;
	}
	if (!bHasBaseSeed)
	{
		BaseSeed = FMath::Rand();
		bHasBaseSeed = true;
	}
	return BaseSeed;
}

void UCSCraftRumorSubsystem::SetRumorBaseSeed(int32 NewSeed)
{
	if (CVarCS_FixedSeed.GetValueOnGameThread() >= 0)
	{
		return;
	}
	BaseSeed = NewSeed;
	bHasBaseSeed = true;
	CachedDay = INDEX_NONE;
	// 시드만 바뀌면 시간대·날씨 묶음은 그대로라 직접 알린다
	OnCraftRumorsChanged.Broadcast();
}

int32 UCSCraftRumorSubsystem::GetBusinessDay() const
{
	const ACSStoreGameState* StoreGameState = GameState.Get();
	return IsValid(StoreGameState) ? StoreGameState->CurrentBusinessDay : 1;
}

int32 UCSCraftRumorSubsystem::GetTimeSlot() const
{
	const ACSStoreGameState* StoreGameState = GameState.Get();
	const UWorld* World = GetWorld();
	if (!IsValid(StoreGameState) || World == nullptr)
	{
		return 0;
	}

	// 영업 시작 시각은 GameMode의 protected 값이고 게터가 없어서 이름으로 읽는다. 마감은 GameState에 사본이 있다
	float StartMinutes = DefaultBusinessStartMinutes;
	if (const ACSStoreGameMode* GameMode = World->GetAuthGameMode<ACSStoreGameMode>())
	{
		if (const FFloatProperty* StartProperty = FindFProperty<FFloatProperty>(ACSStoreGameMode::StaticClass(), TEXT("BusinessStartTimeMinutes")))
		{
			StartMinutes = StartProperty->GetPropertyValue_InContainer(GameMode);
		}
	}

	const float SlotLength = (StoreGameState->AutomaticClosingTimeMinutes - StartMinutes) / TimeSlotsPerDay;
	if (SlotLength <= 0.0f)
	{
		return 0;
	}
	// 영업 전은 첫 시간대로 본다
	const int32 Slot = FMath::FloorToInt((StoreGameState->CurrentGameMinutes - StartMinutes) / SlotLength);
	return FMath::Clamp(Slot, 0, TimeSlotsPerDay - 1);
}

const TArray<FText>& UCSCraftRumorSubsystem::GetDailyRumors(int32 BusinessDay)
{
	if (CachedDay == BusinessDay)
	{
		return CachedDailyRumors;
	}
	CachedDay = BusinessDay;
	CachedDailyRumors.Reset();

	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	if (!IsValid(Crafting) || !Crafting->IsCraftingAvailable())
	{
		// 제조 표가 아직 준비되지 않았으면 다음 호출에서 다시 만든다
		CachedDay = INDEX_NONE;
		return CachedDailyRumors;
	}

	// 재료 성질은 조합을 추리하는 단서라, 제조 전용을 다 찾기 전에 보이면 무엇을 하라는 건지 안 읽힌다
	bool bAllCraftOnlyDiscovered = true;
	for (const FName RecipeRow : Crafting->GetRecipeRowNames(ECSCraftRecipeKind::CraftOnly))
	{
		if (!Crafting->GetDiscoveredRecipeRows().Contains(RecipeRow))
		{
			bAllCraftOnlyDiscovered = false;
			break;
		}
	}

	// 같은 결과물을 가리키는 문장은 한 묶음이고 하루에 하나만 쓴다. 안 그러면 같은 힌트가 말만 바꿔 여러 번 뜬다
	// 결과물이 없는 잡담·성질은 문장마다 따로 묶는다
	TMap<FName, TArray<FCSCraftRumorLine>> Groups;
	for (const FCSCraftRumorLine& Line : Crafting->GetMaterialRumorLines())
	{
		const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(Line.Key);
		Groups.FindOrAdd(Recipe != nullptr ? Recipe->ResultProduct : Line.Key).Add(Line);
	}
	if (const UDataTable* RumorTable = Crafting->GetRumorTable())
	{
		for (const TPair<FName, uint8*>& Pair : RumorTable->GetRowMap())
		{
			const FCSCraftRumorRow* Row = reinterpret_cast<const FCSCraftRumorRow*>(Pair.Value);
			if (Row == nullptr || Row->Text.IsEmpty())
			{
				continue;
			}
			const bool bExcluded = Row->Kind == ECSCraftRumorKind::None ||
				Row->Kind == ECSCraftRumorKind::WeatherVariant ||
				(Row->Kind == ECSCraftRumorKind::Property && !bAllCraftOnlyDiscovered);
			if (!bExcluded)
			{
				Groups.FindOrAdd(Row->TargetProduct.IsNone() ? Pair.Key : Row->TargetProduct).Add({ Pair.Key, Row->Text });
			}
		}
	}

	// 결정적 계산이라 영업일 시작, 1일차, 불러오기 어느 경로로 와도 같은 날은 같은 소문이 나온다.
	// 표 행 순서가 바뀌어도 같은 결과가 나오게 묶음과 문장을 키로 정렬한 뒤 굴린다
	const uint32 DaySeed = HashCombine(HashCombine(GetTypeHash(GetRumorBaseSeed()), RumorSeedSalt), GetTypeHash(BusinessDay));
	FRandomStream Stream(static_cast<int32>(DaySeed));
	Groups.KeySort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	TArray<FCSCraftRumorLine> Pool;
	for (TPair<FName, TArray<FCSCraftRumorLine>>& Group : Groups)
	{
		Group.Value.Sort([](const FCSCraftRumorLine& A, const FCSCraftRumorLine& B) { return A.Key.LexicalLess(B.Key); });
		Pool.Add(Group.Value.Num() > 1 ? Group.Value[Stream.RandRange(0, Group.Value.Num() - 1)] : Group.Value[0]);
	}

	for (int32 Index = Pool.Num() - 1; Index > 0; --Index)
	{
		Pool.Swap(Index, Stream.RandRange(0, Index));
	}

	if (Pool.Num() < RumorsPerDay && !bWarnedSmallPool)
	{
		bWarnedSmallPool = true;
		UE_LOG(LogTemp, Warning, TEXT("[제조 검증] 게시판 소문 풀이 %d개라 하루 %d개를 다 채우지 못합니다."), Pool.Num(), RumorsPerDay);
	}
	for (int32 Index = 0; Index < FMath::Min(Pool.Num(), RumorsPerDay); ++Index)
	{
		CachedDailyRumors.Add(Pool[Index].Text);
	}
	return CachedDailyRumors;
}

FName UCSCraftRumorSubsystem::FindWeatherVariant(FText& OutText)
{
	OutText = FText::GetEmpty();
	UCSWeatherSubsystem* WeatherSubsystem = Weather.Get();
	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	const UDataTable* RumorTable = IsValid(Crafting) ? Crafting->GetRumorTable() : nullptr;
	if (!IsValid(WeatherSubsystem) || RumorTable == nullptr)
	{
		return NAME_None;
	}

	// 날씨 탭이 내일로 보여 주는 날과 같다
	const ECSWeatherType Tomorrow = WeatherSubsystem->GetForecastWeather(1);
	if (Tomorrow == ECSWeatherType::None)
	{
		return NAME_None;
	}

	FName BestProduct;
	float BestModifier = 0.0f;
	for (const TPair<FName, uint8*>& Pair : RumorTable->GetRowMap())
	{
		const FCSCraftRumorRow* Row = reinterpret_cast<const FCSCraftRumorRow*>(Pair.Value);
		if (Row == nullptr || Row->Kind != ECSCraftRumorKind::WeatherVariant || Row->TargetProduct.IsNone())
		{
			continue;
		}
		FDataTableRowHandle ProductRow;
		ProductRow.DataTable = Crafting->GetProductTable();
		ProductRow.RowName = Row->TargetProduct;
		// 눈처럼 둘 다 오르면 더 크게 오르는 쪽, 같으면 표에서 앞선 쪽
		const float Modifier = WeatherSubsystem->GetProductWeatherDemandModifierForWeather(ProductRow, Tomorrow);
		if (Modifier > BestModifier)
		{
			BestModifier = Modifier;
			BestProduct = Row->TargetProduct;
			OutText = Row->Text;
		}
	}
	return BestProduct;
}

TArray<FText> UCSCraftRumorSubsystem::GetCurrentRumors()
{
	TArray<FText> Rumors;
	const TArray<FText>& Daily = GetDailyRumors(GetBusinessDay());
	if (Daily.IsEmpty())
	{
		return Rumors;
	}

	const int32 Slot = GetTimeSlot();
	FText VariantText;
	const FName Variant = FindWeatherVariant(VariantText);
	// 고정 자리가 있으면 하루 9개 중 6개만 쓴다. 추첨은 그대로라 날씨와 상관없이 같은 날 같은 순서다
	const int32 PickCount = Variant.IsNone() ? 3 : 2;
	if (!Variant.IsNone())
	{
		Rumors.Add(VariantText);
	}
	for (int32 Index = Slot * PickCount; Index < (Slot + 1) * PickCount && Daily.IsValidIndex(Index); ++Index)
	{
		Rumors.Add(Daily[Index]);
	}
	return Rumors;
}

void UCSCraftRumorSubsystem::CheckRumorStateChanged()
{
	FText VariantText;
	const int32 Day = GetBusinessDay();
	const int32 Slot = GetTimeSlot();
	const FName Variant = FindWeatherVariant(VariantText);
	if (bHasLastState && Day == LastDay && Slot == LastTimeSlot && Variant == LastVariant)
	{
		return;
	}
	bHasLastState = true;
	LastDay = Day;
	LastTimeSlot = Slot;
	LastVariant = Variant;
	OnCraftRumorsChanged.Broadcast();
}

void UCSCraftRumorSubsystem::HandleGameTimeChanged(float NewGameMinutes)
{
	CheckRumorStateChanged();
}

void UCSCraftRumorSubsystem::HandleBusinessDayChanged(int32 NewBusinessDay)
{
	CheckRumorStateChanged();
}

void UCSCraftRumorSubsystem::HandleWeatherForecastUpdated()
{
	CheckRumorStateChanged();
}
