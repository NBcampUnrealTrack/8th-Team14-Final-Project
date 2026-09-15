#include "Systems/NoticeEvents/CSNoticeEventSubsystem.h"

#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

DEFINE_LOG_CATEGORY_STATIC(LogCSNoticeEvents, Log, All);

// 다른 시스템과 난수 소비 순서를 공유하지 않되 프로젝트의 디버그 고정 시드 규칙은 따릅니다.
extern TAutoConsoleVariable<int32> CVarCS_FixedSeed;

// 랜덤에 맡기면 검증하려는 뉴스가 뜰 때까지 영업일을 반복해야 해서 둔 디버그 스위치
TAutoConsoleVariable<FString> CVarCS_ForcedNews(
	TEXT("cs.news"),
	TEXT("-1"),
	TEXT("cs.news <검색어> — RowName에 검색어가 들어간 뉴스가 딱 1개면 매 영업일 그 뉴스를 강제한다. -1이면 해제. PIE 시작 전에 입력"),
	ECVF_Default);

namespace
{
constexpr float NewsOccurrenceProbability = 0.5f;
constexpr int32 DaysPerMonth = 28;
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

void UCSNoticeEventSubsystem::HandleBusinessDayStarted(const int32 NewBusinessDay)
{
	ProcessBusinessDay(NewBusinessDay);
}

void UCSNoticeEventSubsystem::LoadEventDataTables()
{
	NewsEventDataTable = NewsEventDataTableAsset.LoadSynchronous();
	if (!IsValid(NewsEventDataTable))
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] 뉴스 DataTable을 찾지 못했습니다: %s"),
			*NewsEventDataTableAsset.ToSoftObjectPath().ToString());
	}

	HolidayEventDataTable = HolidayEventDataTableAsset.LoadSynchronous();
	if (!IsValid(HolidayEventDataTable))
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] 기념일 DataTable을 찾지 못했습니다: %s"),
			*HolidayEventDataTableAsset.ToSoftObjectPath().ToString());
	}

	EffectRuleDataTable = EffectRuleDataTableAsset.LoadSynchronous();
	if (!IsValid(EffectRuleDataTable))
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] 수요 효과 규칙 DataTable을 찾지 못했습니다: %s"),
			*EffectRuleDataTableAsset.ToSoftObjectPath().ToString());
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
	CurrentNewsRowName = NAME_None;
	ActiveHolidayRowNames.Reset();

	// 굴림을 건너뛰지 않고 결과만 덮어씀
	SelectDailyNews();
	const bool bNewsForced = ApplyForcedNewsOverride();

	const int32 DayOfMonth = ((SafeBusinessDay - 1) % DaysPerMonth) + 1;
	CollectDailyHolidays(DayOfMonth);

	{
		TArray<FString> HolidayNames;
		HolidayNames.Reserve(ActiveHolidayRowNames.Num());
		for (const FName HolidayRowName : ActiveHolidayRowNames)
		{
			HolidayNames.Add(HolidayRowName.ToString());
		}

		UE_LOG(LogCSNoticeEvents, Log,
			TEXT("[NoticeEvents] %d일차 뉴스=%s%s 기념일=[%s]"),
			SafeBusinessDay,
			CurrentNewsRowName.IsNone() ? TEXT("없음") : *CurrentNewsRowName.ToString(),
			bNewsForced ? TEXT(" (cs.news 강제)") : TEXT(""),
			HolidayNames.IsEmpty() ? TEXT("없음") : *FString::Join(HolidayNames, TEXT(", ")));
	}

	OnNoticeEventsChanged.Broadcast();
}

bool UCSNoticeEventSubsystem::ApplyForcedNewsOverride()
{
	const FString SearchTerm = CVarCS_ForcedNews.GetValueOnGameThread().TrimStartAndEnd();
	if (SearchTerm.IsEmpty() || SearchTerm == TEXT("-1"))
	{
		return false;
	}

	if (!IsValid(NewsEventDataTable))
	{
		return false;
	}

	// SelectDailyNews와 같은 후보 조건 사용
	TArray<FName> MatchedRowNames;
	for (const FName RowName : NewsEventDataTable->GetRowNames())
	{
		const FCSNewsEventDataRow* NewsRow = NewsEventDataTable->FindRow<FCSNewsEventDataRow>(
			RowName,
			TEXT("UCSNoticeEventSubsystem::ApplyForcedNewsOverride"),
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
		CurrentNewsRowName = MatchedRowNames[0];
		return true;
	}

	if (MatchedRowNames.IsEmpty())
	{
		UE_LOG(LogCSNoticeEvents, Warning,
			TEXT("[NoticeEvents] cs.news \"%s\"와 일치하는 뉴스가 없어 랜덤 선택을 유지합니다."),
			*SearchTerm);
		return false;
	}

	TArray<FString> MatchedNames;
	MatchedNames.Reserve(MatchedRowNames.Num());
	for (const FName RowName : MatchedRowNames)
	{
		MatchedNames.Add(RowName.ToString());
	}

	UE_LOG(LogCSNoticeEvents, Warning,
		TEXT("[NoticeEvents] cs.news \"%s\"에 %d개가 일치합니다: [%s]. 더 길게 입력하세요. 랜덤 선택을 유지합니다."),
		*SearchTerm,
		MatchedRowNames.Num(),
		*FString::Join(MatchedNames, TEXT(", ")));
	return false;
}

void UCSNoticeEventSubsystem::SelectDailyNews()
{
	// 매 영업일 첫 굴림의 절반은 DataTable Row가 없는 명시적 "소식 없음" 상태입니다.
	if (NoticeEventRandomStream.FRand() >= NewsOccurrenceProbability ||
		!IsValid(NewsEventDataTable))
	{
		return;
	}

	struct FWeightedNewsCandidate
	{
		FName RowName = NAME_None;
		float Weight = 0.0f;
	};

	TArray<FWeightedNewsCandidate> Candidates;
	float TotalWeight = 0.0f;
	for (const FName RowName : NewsEventDataTable->GetRowNames())
	{
		const FCSNewsEventDataRow* NewsRow = NewsEventDataTable->FindRow<FCSNewsEventDataRow>(
			RowName,
			TEXT("UCSNoticeEventSubsystem::SelectDailyNews"),
			false);
		if (NewsRow == nullptr || !NewsRow->bEnabled || NewsRow->SelectionWeight <= 0.0f)
		{
			continue;
		}

		Candidates.Add({RowName, NewsRow->SelectionWeight});
		TotalWeight += NewsRow->SelectionWeight;
	}

	if (Candidates.IsEmpty() || TotalWeight <= 0.0f)
	{
		return;
	}

	const float SelectionValue = NoticeEventRandomStream.FRand() * TotalWeight;
	float AccumulatedWeight = 0.0f;
	for (const FWeightedNewsCandidate& Candidate : Candidates)
	{
		AccumulatedWeight += Candidate.Weight;
		if (SelectionValue < AccumulatedWeight)
		{
			CurrentNewsRowName = Candidate.RowName;
			return;
		}
	}

	// 부동소수점 누적 오차가 생겨도 마지막 유효 후보가 선택되도록 보장합니다.
	CurrentNewsRowName = Candidates.Last().RowName;
}

void UCSNoticeEventSubsystem::CollectDailyHolidays(const int32 DayOfMonth)
{
	if (!IsValid(HolidayEventDataTable))
	{
		return;
	}

	for (const FName RowName : HolidayEventDataTable->GetRowNames())
	{
		const FCSHolidayEventDataRow* HolidayRow = HolidayEventDataTable->FindRow<FCSHolidayEventDataRow>(
			RowName,
			TEXT("UCSNoticeEventSubsystem::CollectDailyHolidays"),
			false);
		if (HolidayRow != nullptr && HolidayRow->bEnabled && HolidayRow->DayOfMonth == DayOfMonth)
		{
			ActiveHolidayRowNames.Add(RowName);
		}
	}
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
