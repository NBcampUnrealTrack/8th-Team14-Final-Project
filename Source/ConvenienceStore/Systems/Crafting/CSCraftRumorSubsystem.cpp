#include "Systems/Crafting/CSCraftRumorSubsystem.h"

#include "Data/CSCraftTypes.h"
#include "Data/CSWeatherTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "Data/CSProductTypes.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "Systems/NoticeEvents/CSNoticeEventSubsystem.h"
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
	// 영업일 시작과 뉴스 결정 중 어느 쪽이 먼저 불려도 예고가 늦게 바뀌지 않게 뉴스 쪽 방송도 받는다
	if (UCSNoticeEventSubsystem* NoticeSubsystem = InWorld.GetSubsystem<UCSNoticeEventSubsystem>(); IsValid(NoticeSubsystem))
	{
		Notice = NoticeSubsystem;
		NoticeSubsystem->OnNoticeEventsChanged.AddUniqueDynamic(this, &UCSCraftRumorSubsystem::HandleNoticeEventsChanged);
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
	if (UCSNoticeEventSubsystem* NoticeSubsystem = Notice.Get())
	{
		NoticeSubsystem->OnNoticeEventsChanged.RemoveDynamic(this, &UCSCraftRumorSubsystem::HandleNoticeEventsChanged);
	}
	GameState.Reset();
	Weather.Reset();
	Notice.Reset();
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

const TArray<UCSCraftRumorSubsystem::FCSDailyRumor>& UCSCraftRumorSubsystem::GetDailyRumors(int32 BusinessDay)
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

	// 제조 전용을 다 찾기 전에는 그 힌트만, 다 찾은 뒤에는 그 힌트를 빼고 조합·성질·기본 조리법을 낸다.
	// 처음엔 팔 물건을 만드는 법부터 알아야 하고, 이미 아는 레시피를 계속 들으면 소문 자리가 아깝다
	bool bAllCraftOnlyDiscovered = true;
	for (const FName RecipeRow : Crafting->GetRecipeRowNames(ECSCraftRecipeKind::CraftOnly))
	{
		if (!Crafting->GetDiscoveredRecipeRows().Contains(RecipeRow))
		{
			bAllCraftOnlyDiscovered = false;
			break;
		}
	}

	struct FPoolLine
	{
		FName Key;
		FText Text;
		bool bCraftHint = false;
	};

	// 같은 결과물을 가리키는 문장은 한 묶음이고 하루에 하나만 쓴다. 안 그러면 같은 힌트가 말만 바꿔 여러 번 뜬다
	// 결과물이 없는 잡담·성질은 문장마다 따로 묶는다
	TMap<FName, TArray<FPoolLine>> Groups;
	if (!bAllCraftOnlyDiscovered)
	{
		for (const FCSCraftRumorLine& Line : Crafting->GetMaterialRumorLines())
		{
			const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(Line.Key);
			Groups.FindOrAdd(Recipe != nullptr ? Recipe->ResultProduct : Line.Key).Add({ Line.Key, Line.Text, true });
		}
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
			const bool bCraftOnlyHint = Row->Kind == ECSCraftRumorKind::Combination &&
				Crafting->IsCraftOnlyResultProduct(Row->TargetProduct);
			const bool bLaterHint = (Row->Kind == ECSCraftRumorKind::Combination && !bCraftOnlyHint) ||
				Row->Kind == ECSCraftRumorKind::Property;
			// 예고는 내일 일에 맞춰 따로 고르므로 하루 풀에 넣지 않는다
			// 기본 조리법(중불에 세 번)은 조합 힌트 문장마다 들어 있어서 따로 내지 않는다
			const bool bExcluded = Row->Kind == ECSCraftRumorKind::None ||
				Row->Kind == ECSCraftRumorKind::BasicMethod ||
				Row->Kind == ECSCraftRumorKind::WeatherVariant ||
				Row->Kind == ECSCraftRumorKind::NewsPreview ||
				Row->Kind == ECSCraftRumorKind::HolidayPreview ||
				(bCraftOnlyHint && bAllCraftOnlyDiscovered) ||
				(bLaterHint && !bAllCraftOnlyDiscovered);
			if (!bExcluded)
			{
				Groups.FindOrAdd(Row->TargetProduct.IsNone() ? Pair.Key : Row->TargetProduct)
					.Add({ Pair.Key, Row->Text, Row->Kind != ECSCraftRumorKind::Chatter });
			}
		}
	}

	// 결정적 계산이라 영업일 시작, 1일차, 불러오기 어느 경로로 와도 같은 날은 같은 소문이 나온다.
	// 표 행 순서가 바뀌어도 같은 결과가 나오게 묶음과 문장을 키로 정렬한 뒤 굴린다
	const uint32 DaySeed = HashCombine(HashCombine(GetTypeHash(GetRumorBaseSeed()), RumorSeedSalt), GetTypeHash(BusinessDay));
	FRandomStream Stream(static_cast<int32>(DaySeed));
	Groups.KeySort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	TArray<FPoolLine> Pool;
	for (TPair<FName, TArray<FPoolLine>>& Group : Groups)
	{
		Group.Value.Sort([](const FPoolLine& A, const FPoolLine& B) { return A.Key.LexicalLess(B.Key); });
		Pool.Add(Group.Value.Num() > 1 ? Group.Value[Stream.RandRange(0, Group.Value.Num() - 1)] : Group.Value[0]);
	}
	for (int32 Index = Pool.Num() - 1; Index > 0; --Index)
	{
		Pool.Swap(Index, Stream.RandRange(0, Index));
	}

	for (const FPoolLine& Line : Pool)
	{
		CachedDailyRumors.Add({ Line.Text, Line.bCraftHint });
	}
	return CachedDailyRumors;
}

FName UCSCraftRumorSubsystem::FindWeatherVariant(const int32 Slot, FText& OutText)
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

	// 눈처럼 둘 다 오르면 더 크게 오르는 쪽, 같으면 표에서 앞선 쪽
	FName BestProduct;
	float BestModifier = 0.0f;
	TArray<TPair<FName, FText>> Lines;
	for (const TPair<FName, uint8*>& Pair : RumorTable->GetRowMap())
	{
		const FCSCraftRumorRow* Row = reinterpret_cast<const FCSCraftRumorRow*>(Pair.Value);
		if (Row == nullptr || Row->Kind != ECSCraftRumorKind::WeatherVariant || Row->TargetProduct.IsNone())
		{
			continue;
		}
		if (Row->TargetProduct != BestProduct)
		{
			FDataTableRowHandle ProductRow;
			ProductRow.DataTable = Crafting->GetProductTable();
			ProductRow.RowName = Row->TargetProduct;
			const float Modifier = WeatherSubsystem->GetProductWeatherDemandModifierForWeather(ProductRow, Tomorrow);
			if (Modifier <= BestModifier)
			{
				continue;
			}
			BestModifier = Modifier;
			BestProduct = Row->TargetProduct;
			Lines.Reset();
		}
		Lines.Add({ Pair.Key, Row->Text });
	}
	if (Lines.IsEmpty())
	{
		return NAME_None;
	}

	Lines.Sort([](const TPair<FName, FText>& A, const TPair<FName, FText>& B) { return A.Key.LexicalLess(B.Key); });
	OutText = Lines[Slot % Lines.Num()].Value;
	return BestProduct;
}

FName UCSCraftRumorSubsystem::FindEventPreview(const int32 Slot, FText& OutText)
{
	OutText = FText::GetEmpty();
	const UCSNoticeEventSubsystem* NoticeSubsystem = Notice.Get();
	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	const UDataTable* RumorTable = IsValid(Crafting) ? Crafting->GetRumorTable() : nullptr;
	if (!IsValid(NoticeSubsystem) || RumorTable == nullptr)
	{
		return NAME_None;
	}

	// 기념일 날은 뉴스가 안 뜨므로 둘이 겹치지 않는다
	const FName HolidayRowName = NoticeSubsystem->GetTomorrowHolidayRowName();
	const FName NewsRowName = HolidayRowName.IsNone() ? NoticeSubsystem->GetTomorrowNewsRowName() : NAME_None;
	if (HolidayRowName.IsNone() && NewsRowName.IsNone())
	{
		return NAME_None;
	}

	// 기념일은 종족 이야기까지 넣으려고 기념일마다 문장을 따로 쓰고, 뉴스는 틀에 이름과 상품을 끼운다
	TArray<TPair<FName, FText>> Lines;
	for (const TPair<FName, uint8*>& Pair : RumorTable->GetRowMap())
	{
		const FCSCraftRumorRow* Row = reinterpret_cast<const FCSCraftRumorRow*>(Pair.Value);
		const bool bMatches = Row != nullptr && (HolidayRowName.IsNone()
			? Row->Kind == ECSCraftRumorKind::NewsPreview
			: Row->Kind == ECSCraftRumorKind::HolidayPreview && Row->TargetProduct == HolidayRowName);
		if (bMatches)
		{
			Lines.Add({ Pair.Key, Row->Text });
		}
	}
	if (Lines.IsEmpty())
	{
		return NAME_None;
	}
	Lines.Sort([](const TPair<FName, FText>& A, const TPair<FName, FText>& B) { return A.Key.LexicalLess(B.Key); });
	const FText& Template = Lines[Slot % Lines.Num()].Value;

	if (!HolidayRowName.IsNone())
	{
		OutText = Template;
		return HolidayRowName;
	}

	FCSNewsEventDataRow News;
	TArray<FCSNoticeEventEffectRuleRow> Targets;
	NoticeSubsystem->GetPositiveDemandTargets(ECSNoticeEventSourceType::News, NewsRowName, Targets);
	if (!NoticeSubsystem->GetNewsRow(NewsRowName, News) || Targets.IsEmpty())
	{
		// 오르는 상품이 없으면 "무엇을 준비하라"는 예고가 성립하지 않는다
		return NAME_None;
	}

	// 다 나열하면 다시 외울 게 생겨서 가장 많이 오르는 것 둘까지만 말한다. 나머지는 당일 뉴스 탭에 다 나온다
	FText Demand;
	if (Targets[0].TargetType == ECSNoticeEventTargetType::Category)
	{
		const int64 CategoryValue = StaticEnum<ECSProductCategory>()->GetValueByName(Targets[0].TargetId);
		const FText CategoryName = CategoryValue != INDEX_NONE
			? CSGetProductCategoryDisplayName(static_cast<ECSProductCategory>(CategoryValue))
			: FText::FromName(Targets[0].TargetId);
		Demand = FText::Format(NSLOCTEXT("CraftRumor", "PreviewDemandCategory", "{0}|hpp(이라면,라면) 뭐든"), CategoryName);
	}
	else if (Targets.Num() > 1 && Targets[1].TargetType == ECSNoticeEventTargetType::Product)
	{
		Demand = FText::Format(NSLOCTEXT("CraftRumor", "PreviewDemandTwo", "{0}|hpp(이랑,랑) {1}"),
			Crafting->GetProductDisplayName(Targets[0].TargetId), Crafting->GetProductDisplayName(Targets[1].TargetId));
	}
	else
	{
		Demand = Crafting->GetProductDisplayName(Targets[0].TargetId);
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("EventName"), News.Title);
	Args.Add(TEXT("Demand"), Demand);
	OutText = FText::Format(Template, Args);
	return NewsRowName;
}

TArray<FText> UCSCraftRumorSubsystem::GetCurrentRumors()
{
	TArray<FText> Rumors;
	const TArray<FCSDailyRumor>& Daily = GetDailyRumors(GetBusinessDay());
	if (Daily.IsEmpty())
	{
		return Rumors;
	}

	const int32 Slot = GetTimeSlot();
	FText EventText;
	FText WeatherText;
	const bool bHasEvent = !FindEventPreview(Slot, EventText).IsNone();
	const bool bHasWeather = !FindWeatherVariant(Slot, WeatherText).IsNone();

	// 하루치를 앞에서부터 제조 힌트 자리 3개(시간대마다 하나)와 일반 자리로 나눈다.
	// 일반 자리는 시간대마다 둘씩 떼어 두고 예고가 없을 때만 꺼내서, 예고 유무와 상관없이 같은 시간대엔 같은 소문이다
	TArray<const FText*> HintSeats;
	TArray<const FText*> GeneralSeats;
	for (const FCSDailyRumor& Rumor : Daily)
	{
		if (Rumor.bCraftHint && HintSeats.Num() < TimeSlotsPerDay)
		{
			HintSeats.Add(&Rumor.Text);
		}
		else
		{
			GeneralSeats.Add(&Rumor.Text);
		}
	}

	auto AddGeneral = [&Rumors, &GeneralSeats, Slot](const int32 SeatInSlot)
	{
		const int32 Index = Slot * 2 + SeatInSlot;
		if (GeneralSeats.IsValidIndex(Index))
		{
			Rumors.Add(*GeneralSeats[Index]);
		}
	};

	if (bHasEvent)
	{
		Rumors.Add(EventText);
	}
	else
	{
		AddGeneral(0);
	}
	if (bHasWeather)
	{
		Rumors.Add(WeatherText);
	}
	else
	{
		AddGeneral(1);
	}
	if (HintSeats.IsValidIndex(Slot))
	{
		Rumors.Add(*HintSeats[Slot]);
	}
	return Rumors;
}

void UCSCraftRumorSubsystem::CheckRumorStateChanged()
{
	FText Unused;
	const int32 Day = GetBusinessDay();
	const int32 Slot = GetTimeSlot();
	const FName Variant = FindWeatherVariant(Slot, Unused);
	// 게임 시간이 바뀔 때마다 불려서 예고 문장은 조립하지 않고 무슨 일인지만 본다
	FName EventPreview;
	if (const UCSNoticeEventSubsystem* NoticeSubsystem = Notice.Get())
	{
		EventPreview = NoticeSubsystem->GetTomorrowHolidayRowName();
		if (EventPreview.IsNone())
		{
			EventPreview = NoticeSubsystem->GetTomorrowNewsRowName();
		}
	}
	if (bHasLastState && Day == LastDay && Slot == LastTimeSlot && Variant == LastVariant && EventPreview == LastEventPreview)
	{
		return;
	}
	bHasLastState = true;
	LastDay = Day;
	LastTimeSlot = Slot;
	LastVariant = Variant;
	LastEventPreview = EventPreview;
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

void UCSCraftRumorSubsystem::HandleNoticeEventsChanged()
{
	CheckRumorStateChanged();
}
