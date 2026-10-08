#include "Systems/NoticeEvents/CSNoticeEventSubsystem.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Engine/GameInstance.h"

#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

DEFINE_LOG_CATEGORY_STATIC(LogCSNoticeEvents, Log, All);

void UCSNoticeEventSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSNoticeEventSaveRecord& Record = OutSave.NoticeEvent;
	Record.CurrentNewsRowName = CurrentNewsRowName;
	Record.ActiveHolidayRowNames = ActiveHolidayRowNames;

	// 로드 직후 ProcessBusinessDay()가 다시 실행되어 뉴스를 재추첨하는 상황을 방지
	Record.LastProcessedBusinessDay = LastProcessedBusinessDay;
	Record.PreparedNewsRowName = PreparedNewsRowName;
	Record.PreparedNewsDay = PreparedNewsDay;
}

void UCSNoticeEventSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	const FCSNoticeEventSaveRecord& Record = InSave.NoticeEvent;
	CurrentNewsRowName = Record.CurrentNewsRowName;
	ActiveHolidayRowNames = Record.ActiveHolidayRowNames;
	LastProcessedBusinessDay = Record.LastProcessedBusinessDay;
	PreparedNewsRowName = Record.PreparedNewsRowName;
	PreparedNewsDay = Record.PreparedNewsDay;

	OnNoticeEventsChanged.Broadcast();
}

// 다른 시스템과 난수 소비 순서를 공유하지 않되 프로젝트의 디버그 고정 시드 규칙은 따릅니다.
extern TAutoConsoleVariable<int32> CVarCS_FixedSeed;

// 랜덤에 맡기면 검증하려는 뉴스가 뜰 때까지 영업일을 반복해야 해서 둔 디버그 스위치
TAutoConsoleVariable<FString> CVarCS_ForcedNews(
	TEXT("cs.news"),
	TEXT("-1"),
	TEXT("cs.news <검색어> — RowName에 검색어가 들어간 뉴스가 딱 1개면 매 영업일 그 뉴스를 강제한다. none이면 소식 없음, -1이면 해제. PIE 시작 전에 입력"),
	ECVF_Default);

// 기념일은 매월 같은 날이라 검증하려면 그 날짜까지 영업일을 넘겨야 해서 둔 디버그 스위치
TAutoConsoleVariable<FString> CVarCS_ForcedHoliday(
	TEXT("cs.holiday"),
	TEXT("-1"),
	TEXT("cs.holiday <검색어> — RowName에 검색어가 들어간 기념일이 딱 1개면 매 영업일 그 기념일만 연다. none이면 기념일 없음, -1이면 해제. PIE 시작 전에 입력"),
	ECVF_Default);

namespace
{
constexpr float NewsOccurrenceProbability = 0.5f;
constexpr int32 DaysPerMonth = 28;

// 처음 며칠은 진열·판매·가격만 익히게 뉴스와 기념일을 띄우지 않는다
constexpr int32 QuietOpeningDays = 2;

// 뉴스와 기념일이 같은 종족을 겹쳐 올려도 한 종족이 손님을 독차지하지 않게 막는 상한.
// 하한은 0이 되면 그 종족이 아예 안 나와서 둔다
constexpr float MinRaceSpawnMultiplier = 0.1f;
constexpr float MaxRaceSpawnMultiplier = 3.0f;
}

void UCSNoticeEventSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const int32 FixedSeed = CVarCS_FixedSeed.GetValueOnGameThread();
	NoticeEventRandomStream.Initialize(FixedSeed >= 0 ? FixedSeed : FMath::Rand());
	LoadEventDataTables();

	SubscribedStoreGameState = InWorld.GetGameState<ACSStoreGameState>();
	if (!IsValid(SubscribedStoreGameState))
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] ACSStoreGameState를 찾지 못해 일일 이벤트를 초기화하지 못했습니다."));
		return;
	}

	SubscribedStoreGameState->OnBusinessDayStarted.AddUniqueDynamic(
		this,
		&UCSNoticeEventSubsystem::HandleBusinessDayStarted);
	ProcessBusinessDay(SubscribedStoreGameState->CurrentBusinessDay);
}

void UCSNoticeEventSubsystem::Deinitialize()
{
	if (IsValid(SubscribedStoreGameState))
	{
		SubscribedStoreGameState->OnBusinessDayStarted.RemoveDynamic(
			this,
			&UCSNoticeEventSubsystem::HandleBusinessDayStarted);
	}

	SubscribedStoreGameState = nullptr;
	NewsEventDataTable = nullptr;
	HolidayEventDataTable = nullptr;
	EffectRuleDataTable = nullptr;
	CurrentNewsRowName = NAME_None;
	ActiveHolidayRowNames.Reset();
	LastProcessedBusinessDay = INDEX_NONE;
	PreparedNewsRowName = NAME_None;
	PreparedNewsDay = INDEX_NONE;

	Super::Deinitialize();
}

bool UCSNoticeEventSubsystem::HasTodayNews() const
{
	return !CurrentNewsRowName.IsNone();
}

bool UCSNoticeEventSubsystem::GetTodayNews(FCSNewsEventDataRow& OutNews) const
{
	OutNews = FCSNewsEventDataRow();
	if (!IsValid(NewsEventDataTable) || CurrentNewsRowName.IsNone())
	{
		return false;
	}

	const FCSNewsEventDataRow* NewsRow = NewsEventDataTable->FindRow<FCSNewsEventDataRow>(
		CurrentNewsRowName,
		TEXT("UCSNoticeEventSubsystem::GetTodayNews"),
		false);
	if (NewsRow == nullptr)
	{
		return false;
	}

	OutNews = *NewsRow;
	return true;
}

void UCSNoticeEventSubsystem::GetTodayNewsDemandChanges(
	TArray<FCSNoticeBoardDemandChange>& OutChanges) const
{
	OutChanges.Reset();
	if (!IsValid(EffectRuleDataTable))
	{
		return;
	}

	const UDataTable* ProductDataTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/ConvenienceStore/Data/DT_ProductMaster.DT_ProductMaster"));
	const UEnum* ProductCategoryEnum = StaticEnum<ECSProductCategory>();

	TArray<FCSNoticeEventEffectRuleRow*> EffectRules;
	EffectRuleDataTable->GetAllRows(
		TEXT("UCSNoticeEventSubsystem::GetTodayNewsDemandChanges"),
		EffectRules);

	for (const FCSNoticeEventEffectRuleRow* Rule : EffectRules)
	{
		// 종족 효과는 상품 행으로 보여줄 수 없어서 기념일·뉴스 설명 문장으로 알린다
		if (Rule == nullptr || !Rule->bEnabled ||
			Rule->TargetType == ECSNoticeEventTargetType::Race ||
			!IsEffectSourceActive(*Rule) ||
			FMath::IsNearlyZero(Rule->DemandModifier))
		{
			continue;
		}

		FCSNoticeBoardDemandChange& Change = OutChanges.AddDefaulted_GetRef();
		Change.TargetType = Rule->TargetType;
		Change.TargetId = Rule->TargetId;
		Change.DemandModifier = Rule->DemandModifier;
		Change.bIsIncrease = Rule->DemandModifier > 0.0f;
		Change.SourceType = Rule->SourceType;

		if (Rule->TargetType == ECSNoticeEventTargetType::Product &&
			IsValid(ProductDataTable))
		{
			if (const FCSProductDataRow* Product = ProductDataTable->FindRow<FCSProductDataRow>(
				Rule->TargetId,
				TEXT("UCSNoticeEventSubsystem::GetTodayNewsDemandChanges"),
				false))
			{
				Change.TargetDisplayName = Product->DisplayName;
			}
		}
		else if (Rule->TargetType == ECSNoticeEventTargetType::Category &&
			ProductCategoryEnum != nullptr)
		{
			const int64 CategoryValue = ProductCategoryEnum->GetValueByName(Rule->TargetId);
			if (CategoryValue != INDEX_NONE)
			{
				// 카테고리 행이 상품 한 개로 읽히지 않게 "전체"를 붙인다
				Change.TargetDisplayName = FText::Format(NSLOCTEXT("NoticeBoard", "CategoryAll", "{0} 전체"),
					CSGetProductCategoryDisplayName(static_cast<ECSProductCategory>(CategoryValue)));
			}
		}

		if (Change.TargetDisplayName.IsEmpty())
		{
			Change.TargetDisplayName = FText::FromName(Rule->TargetId);
		}

		// 수요 칸은 좁아서 기념일 표시는 이름 쪽에 붙인다
		if (Rule->SourceType == ECSNoticeEventSourceType::Holiday)
		{
			Change.TargetDisplayName = FText::Format(NSLOCTEXT("NoticeBoard", "HolidayTarget", "{0} (기념일)"),
				Change.TargetDisplayName);
		}
	}
}

FName UCSNoticeEventSubsystem::GetTodayNewsRowName() const
{
	return CurrentNewsRowName;
}

bool UCSNoticeEventSubsystem::HasTodayHoliday() const
{
	return ActiveHolidayRowNames.Num() > 0;
}

bool UCSNoticeEventSubsystem::GetTodayHoliday(FCSHolidayEventDataRow& OutHoliday) const
{
	OutHoliday = FCSHolidayEventDataRow();
	if (!IsValid(HolidayEventDataTable) || ActiveHolidayRowNames.IsEmpty())
	{
		return false;
	}

	const FCSHolidayEventDataRow* HolidayRow = HolidayEventDataTable->FindRow<FCSHolidayEventDataRow>(
		ActiveHolidayRowNames[0],
		TEXT("UCSNoticeEventSubsystem::GetTodayHoliday"),
		false);
	if (HolidayRow == nullptr)
	{
		return false;
	}

	OutHoliday = *HolidayRow;
	return true;
}

void UCSNoticeEventSubsystem::GetTodayHolidays(TArray<FCSHolidayEventDataRow>& OutHolidays) const
{
	OutHolidays.Reset();
	if (!IsValid(HolidayEventDataTable))
	{
		return;
	}

	for (const FName HolidayRowName : ActiveHolidayRowNames)
	{
		const FCSHolidayEventDataRow* HolidayRow = HolidayEventDataTable->FindRow<FCSHolidayEventDataRow>(
			HolidayRowName,
			TEXT("UCSNoticeEventSubsystem::GetTodayHolidays"),
			false);
		if (HolidayRow != nullptr)
		{
			OutHolidays.Add(*HolidayRow);
		}
	}
}

float UCSNoticeEventSubsystem::GetProductDemandModifier(
	const FDataTableRowHandle& ProductRow) const
{
	if (!IsValid(EffectRuleDataTable) ||
		!IsValid(ProductRow.DataTable) ||
		ProductRow.RowName.IsNone())
	{
		return 0.0f;
	}

	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("UCSNoticeEventSubsystem::GetProductDemandModifier"));
	if (ProductData == nullptr)
	{
		return 0.0f;
	}

	FName ProductCategoryName = NAME_None;
	if (const UEnum* ProductCategoryEnum = StaticEnum<ECSProductCategory>())
	{
		ProductCategoryName = FName(*ProductCategoryEnum->GetNameStringByValue(
			static_cast<int64>(ProductData->Category)));
	}

	float TotalModifier = 0.0f;
	TArray<FCSNoticeEventEffectRuleRow*> EffectRules;
	EffectRuleDataTable->GetAllRows(
		TEXT("UCSNoticeEventSubsystem::GetProductDemandModifier"),
		EffectRules);

	for (const FCSNoticeEventEffectRuleRow* Rule : EffectRules)
	{
		if (Rule == nullptr || !Rule->bEnabled || !IsEffectSourceActive(*Rule))
		{
			continue;
		}

		const bool bTargetMatches =
			(Rule->TargetType == ECSNoticeEventTargetType::Product &&
				ProductRow.RowName == Rule->TargetId) ||
			(Rule->TargetType == ECSNoticeEventTargetType::Category &&
				!ProductCategoryName.IsNone() &&
				ProductCategoryName == Rule->TargetId);
		if (bTargetMatches)
		{
			TotalModifier += Rule->DemandModifier;
		}
	}

	return TotalModifier;
}

float UCSNoticeEventSubsystem::GetProductDemandMultiplier(
	const FDataTableRowHandle& ProductRow) const
{
	return FMath::Max(0.0f, 1.0f + GetProductDemandModifier(ProductRow));
}

bool UCSNoticeEventSubsystem::GetStrongestPositiveDemandSource(
	const FDataTableRowHandle& ProductRow,
	ECSNoticeEventSourceType& OutSource) const
{
	if (!IsValid(EffectRuleDataTable) ||
		!IsValid(ProductRow.DataTable) ||
		ProductRow.RowName.IsNone())
	{
		return false;
	}

	const FCSProductDataRow* ProductData = ProductRow.GetRow<FCSProductDataRow>(
		TEXT("UCSNoticeEventSubsystem::GetStrongestPositiveDemandSource"));
	if (ProductData == nullptr)
	{
		return false;
	}

	FName ProductCategoryName = NAME_None;
	if (const UEnum* ProductCategoryEnum = StaticEnum<ECSProductCategory>())
	{
		ProductCategoryName = FName(*ProductCategoryEnum->GetNameStringByValue(
			static_cast<int64>(ProductData->Category)));
	}

	TArray<FCSNoticeEventEffectRuleRow*> EffectRules;
	EffectRuleDataTable->GetAllRows(
		TEXT("UCSNoticeEventSubsystem::GetStrongestPositiveDemandSource"),
		EffectRules);

	// 합산이 아니라 개별 규칙의 최댓값으로 고른다 - 상품 지정 규칙과 카테고리 규칙이
	// 같은 상품에 겹칠 때, 더 구체적으로 지목한 쪽이 문구의 주인이어야 자연스럽다
	float BestModifier = 0.0f;
	bool bFound = false;

	for (const FCSNoticeEventEffectRuleRow* Rule : EffectRules)
	{
		if (Rule == nullptr || !Rule->bEnabled || Rule->DemandModifier <= 0.0f || !IsEffectSourceActive(*Rule))
		{
			continue;
		}

		const bool bTargetMatches =
			(Rule->TargetType == ECSNoticeEventTargetType::Product &&
				ProductRow.RowName == Rule->TargetId) ||
			(Rule->TargetType == ECSNoticeEventTargetType::Category &&
				!ProductCategoryName.IsNone() &&
				ProductCategoryName == Rule->TargetId);
		if (!bTargetMatches)
		{
			continue;
		}

		if (!bFound || Rule->DemandModifier > BestModifier)
		{
			BestModifier = Rule->DemandModifier;
			OutSource = Rule->SourceType;
			bFound = true;
		}
	}

	return bFound;
}

float UCSNoticeEventSubsystem::GetRaceSpawnMultiplier(const FName ArchetypeId) const
{
	if (!IsValid(EffectRuleDataTable) || ArchetypeId.IsNone())
	{
		return 1.0f;
	}

	TArray<FCSNoticeEventEffectRuleRow*> EffectRules;
	EffectRuleDataTable->GetAllRows(TEXT("UCSNoticeEventSubsystem::GetRaceSpawnMultiplier"), EffectRules);

	// 수요 보정과 같은 방식으로 더한다. 곱하면 뉴스와 기념일이 겹친 날 배수가 너무 빨리 커진다
	float TotalModifier = 0.0f;
	for (const FCSNoticeEventEffectRuleRow* Rule : EffectRules)
	{
		if (Rule != nullptr && Rule->bEnabled && Rule->TargetType == ECSNoticeEventTargetType::Race &&
			Rule->TargetId == ArchetypeId && IsEffectSourceActive(*Rule))
		{
			TotalModifier += Rule->DemandModifier;
		}
	}

	return FMath::Clamp(1.0f + TotalModifier, MinRaceSpawnMultiplier, MaxRaceSpawnMultiplier);
}

void UCSNoticeEventSubsystem::HandleBusinessDayStarted(const int32 NewBusinessDay)
{
	ProcessBusinessDay(NewBusinessDay);
}

void UCSNoticeEventSubsystem::LoadEventDataTables()
{
	const ACSStoreGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACSStoreGameMode>() : nullptr;
	NewsEventDataTable = GameMode ? GameMode->GetNewsEventDataTable() : nullptr;
	if (!IsValid(NewsEventDataTable))
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] GameMode의 NewsEventDataTable을 지정해 주세요."));
	}

	HolidayEventDataTable = GameMode ? GameMode->GetHolidayEventDataTable() : nullptr;
	if (!IsValid(HolidayEventDataTable))
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] GameMode의 HolidayEventDataTable을 지정해 주세요."));
	}

	EffectRuleDataTable = GameMode ? GameMode->GetNoticeEventEffectRuleDataTable() : nullptr;
	if (!IsValid(EffectRuleDataTable))
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] GameMode의 NoticeEventEffectRuleDataTable을 지정해 주세요."));
	}
}

void UCSNoticeEventSubsystem::ProcessBusinessDay(const int32 BusinessDay)
{
	const int32 SafeBusinessDay = FMath::Max(1, BusinessDay);
	if (LastProcessedBusinessDay == SafeBusinessDay)
	{
		return;
	}

	LastProcessedBusinessDay = SafeBusinessDay;

	// 내일 뉴스는 오늘 미리 굴려 둬야 소문으로 예고할 수 있다. 영업일마다 굴리는 순서는 그대로라
	// 같은 시드면 예전과 같은 날 같은 뉴스가 나온다. 첫날이나 불러온 직후처럼 준비된 게 없으면 오늘 것도 굴린다
	const FName RolledTodayNews = PreparedNewsDay == SafeBusinessDay ? PreparedNewsRowName : RollNewsForDay(SafeBusinessDay);
	PreparedNewsRowName = RollNewsForDay(SafeBusinessDay + 1);
	PreparedNewsDay = SafeBusinessDay + 1;

	bool bHolidayForced = false;
	ActiveHolidayRowNames = GetHolidayRowNamesForDay(SafeBusinessDay, bHolidayForced, true);
	bool bNewsForced = false;
	CurrentNewsRowName = ResolveNewsForDay(SafeBusinessDay, RolledTodayNews, !ActiveHolidayRowNames.IsEmpty(), bNewsForced, true);

	{
		TArray<FString> HolidayNames;
		HolidayNames.Reserve(ActiveHolidayRowNames.Num());
		for (const FName HolidayRowName : ActiveHolidayRowNames)
		{
			HolidayNames.Add(HolidayRowName.ToString());
		}

		UE_LOG(LogCSNoticeEvents, Log,
			TEXT("[NoticeEvents] %d일차 뉴스=%s%s 기념일=[%s]%s 내일뉴스=%s"),
			SafeBusinessDay,
			CurrentNewsRowName.IsNone() ? TEXT("없음") : *CurrentNewsRowName.ToString(),
			bNewsForced ? TEXT(" (cs.news 강제)") : TEXT(""),
			HolidayNames.IsEmpty() ? TEXT("없음") : *FString::Join(HolidayNames, TEXT(", ")),
			bHolidayForced ? TEXT(" (cs.holiday 강제)") : TEXT(""),
			*GetTomorrowNewsRowName().ToString());
	}

	// 오늘의 뉴스와 기념일을 날짜별 수첩 기록으로 남겨 이후에도 확인하실 수 있도록 합니다.
	if (auto* GI = GetWorld()->GetGameInstance())
	{
		auto* Journal = GI->GetSubsystem<UCSJournalSubsystem>();
		Journal->ObserveDay(SafeBusinessDay);
		FCSNewsEventDataRow News;
		if (GetTodayNews(News)) Journal->RecordEvent(FName(*(TEXT("News.") + CurrentNewsRowName.ToString())), SafeBusinessDay, News.Title, News.Body);
		for (FName Id : ActiveHolidayRowNames)
		{
			const auto* Holiday = HolidayEventDataTable->FindRow<FCSHolidayEventDataRow>(Id, TEXT("Journal"), false);
			if (Holiday) Journal->RecordEvent(FName(*(TEXT("Holiday.") + Id.ToString())), SafeBusinessDay, Holiday->Title, Holiday->Description);
		}
	}
	OnNoticeEventsChanged.Broadcast();
}

FName UCSNoticeEventSubsystem::ResolveNewsForDay(const int32 Day, const FName RolledNews, const bool bHasHoliday, bool& bOutForced, const bool bLogMismatch) const
{
	FName ForcedNews;
	bOutForced = TryGetForcedNews(ForcedNews, bLogMismatch);
	if (bOutForced)
	{
		// 강제 명령은 겹침이나 첫날 검증용이라 아래 규칙을 거치지 않는다
		return ForcedNews;
	}

	// 기념일에 뉴스까지 겹치면 날씨와 합쳐 수요가 오르는 상품이 너무 많아진다
	if (bHasHoliday || Day <= QuietOpeningDays)
	{
		return NAME_None;
	}
	return RolledNews;
}

TArray<FName> UCSNoticeEventSubsystem::GetHolidayRowNamesForDay(const int32 Day, bool& bOutForced, const bool bLogMismatch) const
{
	TArray<FName> HolidayRowNames;
	bOutForced = TryGetForcedHolidays(HolidayRowNames, bLogMismatch);
	if (bOutForced)
	{
		return HolidayRowNames;
	}
	if (Day <= QuietOpeningDays || !IsValid(HolidayEventDataTable))
	{
		return HolidayRowNames;
	}

	const int32 DayOfMonth = ((FMath::Max(1, Day) - 1) % DaysPerMonth) + 1;
	for (const FName RowName : HolidayEventDataTable->GetRowNames())
	{
		const FCSHolidayEventDataRow* HolidayRow = HolidayEventDataTable->FindRow<FCSHolidayEventDataRow>(
			RowName,
			TEXT("UCSNoticeEventSubsystem::GetHolidayRowNamesForDay"),
			false);
		if (HolidayRow != nullptr && HolidayRow->bEnabled && HolidayRow->DayOfMonth == DayOfMonth)
		{
			HolidayRowNames.Add(RowName);
		}
	}
	return HolidayRowNames;
}

FName UCSNoticeEventSubsystem::GetTomorrowNewsRowName() const
{
	const int32 Tomorrow = LastProcessedBusinessDay + 1;
	bool bForced = false;
	const bool bHasHoliday = !GetHolidayRowNamesForDay(Tomorrow, bForced, false).IsEmpty();
	const FName Rolled = PreparedNewsDay == Tomorrow ? PreparedNewsRowName : NAME_None;
	return ResolveNewsForDay(Tomorrow, Rolled, bHasHoliday, bForced, false);
}

FName UCSNoticeEventSubsystem::GetTomorrowHolidayRowName() const
{
	bool bForced = false;
	const TArray<FName> HolidayRowNames = GetHolidayRowNamesForDay(LastProcessedBusinessDay + 1, bForced, false);
	return HolidayRowNames.IsEmpty() ? NAME_None : HolidayRowNames[0];
}

bool UCSNoticeEventSubsystem::GetNewsRow(const FName NewsRowName, FCSNewsEventDataRow& OutNews) const
{
	const FCSNewsEventDataRow* NewsRow = IsValid(NewsEventDataTable) && !NewsRowName.IsNone()
		? NewsEventDataTable->FindRow<FCSNewsEventDataRow>(NewsRowName, TEXT("UCSNoticeEventSubsystem::GetNewsRow"), false)
		: nullptr;
	if (NewsRow == nullptr)
	{
		return false;
	}
	OutNews = *NewsRow;
	return true;
}

void UCSNoticeEventSubsystem::GetPositiveDemandTargets(const ECSNoticeEventSourceType SourceType, const FName SourceId,
	TArray<FCSNoticeEventEffectRuleRow>& OutTargets) const
{
	OutTargets.Reset();
	if (!IsValid(EffectRuleDataTable) || SourceId.IsNone())
	{
		return;
	}

	for (const TPair<FName, uint8*>& Pair : EffectRuleDataTable->GetRowMap())
	{
		const FCSNoticeEventEffectRuleRow* Rule = reinterpret_cast<const FCSNoticeEventEffectRuleRow*>(Pair.Value);
		if (Rule != nullptr && Rule->bEnabled && Rule->SourceType == SourceType && Rule->SourceId == SourceId &&
			Rule->TargetType != ECSNoticeEventTargetType::Race && Rule->DemandModifier > 0.0f)
		{
			OutTargets.Add(*Rule);
		}
	}

	// 많이 오르는 순. 같으면 카테고리를 앞에 둔다. 카테고리 전체가 오르는 게 플레이어에게 더 큰 소식이다
	OutTargets.Sort([](const FCSNoticeEventEffectRuleRow& A, const FCSNoticeEventEffectRuleRow& B)
	{
		if (!FMath::IsNearlyEqual(A.DemandModifier, B.DemandModifier))
		{
			return A.DemandModifier > B.DemandModifier;
		}
		if (A.TargetType != B.TargetType)
		{
			return A.TargetType == ECSNoticeEventTargetType::Category;
		}
		return A.TargetId.LexicalLess(B.TargetId);
	});
}

bool UCSNoticeEventSubsystem::TryGetForcedNews(FName& OutNewsRowName, const bool bLogMismatch) const
{
	OutNewsRowName = NAME_None;
	const FString SearchTerm = CVarCS_ForcedNews.GetValueOnGameThread().TrimStartAndEnd();
	if (SearchTerm.IsEmpty() || SearchTerm == TEXT("-1"))
	{
		return false;
	}
	if (SearchTerm.Equals(TEXT("none"), ESearchCase::IgnoreCase) ||
		SearchTerm.Equals(TEXT("없음"), ESearchCase::CaseSensitive))
	{
		return true;
	}

	if (!IsValid(NewsEventDataTable))
	{
		return false;
	}

	// RollNewsForDay와 같은 후보 조건 사용
	TArray<FName> MatchedRowNames;
	for (const FName RowName : NewsEventDataTable->GetRowNames())
	{
		const FCSNewsEventDataRow* NewsRow = NewsEventDataTable->FindRow<FCSNewsEventDataRow>(
			RowName,
			TEXT("UCSNoticeEventSubsystem::TryGetForcedNews"),
			false);
		if (NewsRow == nullptr || !NewsRow->bEnabled || NewsRow->SelectionWeight <= 0.0f)
		{
			continue;
		}

		if (RowName.ToString().Contains(SearchTerm, ESearchCase::IgnoreCase))
		{
			MatchedRowNames.Add(RowName);
		}
	}

	if (MatchedRowNames.Num() == 1)
	{
		OutNewsRowName = MatchedRowNames[0];
		return true;
	}

	if (bLogMismatch)
	{
		TArray<FString> MatchedNames;
		MatchedNames.Reserve(MatchedRowNames.Num());
		for (const FName RowName : MatchedRowNames)
		{
			MatchedNames.Add(RowName.ToString());
		}
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] cs.news \"%s\"와 일치하는 뉴스가 %d개입니다: [%s]. 딱 1개가 되게 입력하세요. 랜덤 선택을 유지합니다."),
			*SearchTerm,
			MatchedRowNames.Num(),
			*FString::Join(MatchedNames, TEXT(", ")));
	}
	return false;
}

FName UCSNoticeEventSubsystem::RollNewsForDay(const int32 Day)
{
	// 조용한 첫 며칠 다음 날은 처음 변화를 확실히 보여 주려고 뉴스를 반드시 띄운다.
	// 그날도 발생 굴림은 똑같이 한 번 소비해서 다른 날과 굴림 순서를 맞춘다
	const bool bGuaranteedDay = Day == QuietOpeningDays + 1;

	// 매 영업일 첫 굴림의 절반은 DataTable Row가 없는 명시적 "소식 없음" 상태입니다.
	const bool bOccurs = NoticeEventRandomStream.FRand() < NewsOccurrenceProbability;
	if ((!bOccurs && !bGuaranteedDay) || !IsValid(NewsEventDataTable))
	{
		return NAME_None;
	}

	struct FWeightedNewsCandidate
	{
		FName RowName = NAME_None;
		float Weight = 0.0f;
	};

	TArray<FWeightedNewsCandidate> Candidates;
	float TotalWeight = 0.0f;
	TArray<FCSNoticeEventEffectRuleRow> RaisedTargets;
	for (const FName RowName : NewsEventDataTable->GetRowNames())
	{
		const FCSNewsEventDataRow* NewsRow = NewsEventDataTable->FindRow<FCSNewsEventDataRow>(
			RowName,
			TEXT("UCSNoticeEventSubsystem::RollNewsForDay"),
			false);
		if (NewsRow == nullptr || !NewsRow->bEnabled || NewsRow->SelectionWeight <= 0.0f)
		{
			continue;
		}

		// 수요를 깎기만 하는 뉴스는 처음 보는 플레이어에게 할 일을 주지 않아서 보장하는 날에는 뺀다
		if (bGuaranteedDay && IsValid(EffectRuleDataTable))
		{
			GetPositiveDemandTargets(ECSNoticeEventSourceType::News, RowName, RaisedTargets);
			if (RaisedTargets.IsEmpty())
			{
				continue;
			}
		}

		Candidates.Add({RowName, NewsRow->SelectionWeight});
		TotalWeight += NewsRow->SelectionWeight;
	}

	if (Candidates.IsEmpty() || TotalWeight <= 0.0f)
	{
		return NAME_None;
	}

	const float SelectionValue = NoticeEventRandomStream.FRand() * TotalWeight;
	float AccumulatedWeight = 0.0f;
	for (const FWeightedNewsCandidate& Candidate : Candidates)
	{
		AccumulatedWeight += Candidate.Weight;
		if (SelectionValue < AccumulatedWeight)
		{
			return Candidate.RowName;
		}
	}

	// 부동소수점 누적 오차가 생겨도 마지막 유효 후보가 선택되도록 보장합니다.
	return Candidates.Last().RowName;
}

bool UCSNoticeEventSubsystem::TryGetForcedHolidays(TArray<FName>& OutHolidayRowNames, const bool bLogMismatch) const
{
	OutHolidayRowNames.Reset();
	const FString SearchTerm = CVarCS_ForcedHoliday.GetValueOnGameThread().TrimStartAndEnd();
	if (SearchTerm.IsEmpty() || SearchTerm == TEXT("-1"))
	{
		return false;
	}
	if (SearchTerm.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	if (!IsValid(HolidayEventDataTable))
	{
		return false;
	}

	TArray<FName> MatchedRowNames;
	for (const FName RowName : HolidayEventDataTable->GetRowNames())
	{
		const FCSHolidayEventDataRow* HolidayRow = HolidayEventDataTable->FindRow<FCSHolidayEventDataRow>(
			RowName, TEXT("UCSNoticeEventSubsystem::TryGetForcedHolidays"), false);
		if (HolidayRow != nullptr && HolidayRow->bEnabled &&
			RowName.ToString().Contains(SearchTerm, ESearchCase::IgnoreCase))
		{
			MatchedRowNames.Add(RowName);
		}
	}

	if (MatchedRowNames.Num() != 1)
	{
		if (bLogMismatch)
		{
			UE_LOG(LogCSNoticeEvents, Warning,
				TEXT("[NoticeEvents] cs.holiday \"%s\"와 일치하는 기념일이 %d개라 날짜대로 둡니다."),
				*SearchTerm, MatchedRowNames.Num());
		}
		return false;
	}

	OutHolidayRowNames = MatchedRowNames;
	return true;
}

bool UCSNoticeEventSubsystem::IsEffectSourceActive(
	const FCSNoticeEventEffectRuleRow& Rule) const
{
	switch (Rule.SourceType)
	{
	case ECSNoticeEventSourceType::News:
		return !CurrentNewsRowName.IsNone() && CurrentNewsRowName == Rule.SourceId;

	case ECSNoticeEventSourceType::Holiday:
		return ActiveHolidayRowNames.Contains(Rule.SourceId);

	default:
		return false;
	}
}
