#include "Systems/Journal/CSJournalSubsystem.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Data/CSJournalSettings.h"

void UCSJournalSubsystem::ResetJournal()
{
	UnlockedEntries.Reset();
	CollectedArtifacts.Reset();
	Events.Reset();
	Deliveries.Reset();
	LatestDay = 1;
	OnJournalChanged.Broadcast();
}
// 도달한 영업일이 증가하면 달력의 열람 범위를 확장하고 화면에 변경을 알립니다.
void UCSJournalSubsystem::ObserveDay(int32 Day)
{
	const int32 NewDay = FMath::Max(LatestDay, FMath::Max(1, Day));
	if (NewDay != LatestDay)
	{
		LatestDay = NewDay;
		OnJournalChanged.Broadcast();
	}
}
// 비어 있는 ID와 이미 해금한 ID를 제외하고 새로운 종족 정보를 등록합니다.
bool UCSJournalSubsystem::UnlockEntry(FName Id)
{
	if (Id.IsNone() || UnlockedEntries.Contains(Id))
		return false;
	UnlockedEntries.Add(Id);
	OnJournalChanged.Broadcast();
	return true;
}
// 새 아티팩트를 획득한 경우에만 수집 상태를 변경하고 도감 화면에 알립니다.
bool UCSJournalSubsystem::CollectArtifact(FName Id)
{
	if (Id.IsNone() || CollectedArtifacts.Contains(Id))
		return false;
	CollectedArtifacts.Add(Id);
	OnJournalChanged.Broadcast();
	return true;
}
// 같은 이벤트 ID와 같은 영업일의 중복을 방지하며 다른 날짜의 발생 기록은 누적합니다.
void UCSJournalSubsystem::RecordEvent(FName Id, int32 Day, FText Title, FText Body)
{
	if (Id.IsNone())
		return;
	Day = FMath::Max(1, Day);
	ObserveDay(Day);
	for (const auto &E : Events)
		if (E.EventId == Id && E.BusinessDay == Day)
			return;
	FCSJournalEvent E;
	E.EventId = Id;
	E.BusinessDay = Day;
	E.Title = Title;
	E.Body = Body;
	Events.Add(E);
	OnJournalChanged.Broadcast();
}
// 선택한 날짜의 기록만 읽음 상태로 변경하여 다른 날짜의 알림을 유지합니다.
void UCSJournalSubsystem::MarkDayRead(int32 Day)
{
	bool Changed = false;
	for (auto &E : Events)
		if (E.BusinessDay == Day && !E.bRead)
		{
			E.bRead = true;
			Changed = true;
		}
	if (Changed)
		OnJournalChanged.Broadcast();
}
// 월드별 주문 ID로 진행 중 기록을 갱신하며 주문이 종료되면 종료일을 기록합니다.
void UCSJournalSubsystem::RecordDelivery(const FCSDeliveryOrderRuntime &Order, int32 Day, FName LevelId)
{
	int32 Index = Deliveries.IndexOfByPredicate(
		[&](const FCSJournalDelivery &D)
		{ return D.LevelId == LevelId && D.Order.OrderId == Order.OrderId && D.FinishedDay == 0; });
	if (Index == INDEX_NONE)
	{
		// 플레이어가 수락한 주문만 배달 기록에 추가합니다. 미수락 주문의 만료는 제외합니다.
		if (Order.Status != ECSDeliveryOrderStatus::Accepted)
			return;
		FCSJournalDelivery D;
		D.RecordId = FGuid::NewGuid();
		D.LevelId = LevelId;
		D.AcceptedDay = Day;
		D.Order = Order;
		Index = Deliveries.Add(D);
	}
	auto &D = Deliveries[Index];
	D.Order = Order;
	if (Order.Status != ECSDeliveryOrderStatus::Accepted)
		D.FinishedDay = Day;
	ObserveDay(Day);
	OnJournalChanged.Broadcast();
}

// 기존 종족 성향 에셋에서 기본 상품 취향 설명을 구성합니다. 사용자 카탈로그로 대체하실 수 있습니다.
void UCSJournalSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);
	// 프로젝트 설정에 등록한 카탈로그가 있으면 모든 발견 지점이 같은 목록을 사용합니다.
	if (auto* ConfiguredCatalog = GetDefault<UCSJournalSettings>()->Catalog.LoadSynchronous();
		IsValid(ConfiguredCatalog) && !ConfiguredCatalog->RaceEntries.IsEmpty())
	{
		Catalog = ConfiguredCatalog;
		return;
	}

	// 공용 에셋에 내용이 아직 없을 때에도 기존 게임 진행을 유지하는 초기값입니다.
	Catalog = NewObject<UCSJournalCatalog>(this);
	const TCHAR *Races[] = {TEXT("Goblin"), TEXT("Orc"), TEXT("Human"), TEXT("Wizard"), TEXT("Dwarf"), TEXT("Elf")};
	const TCHAR *Assets[] = {TEXT("10s_Goblin"), TEXT("40s_Orc"),	TEXT("20s_Adventurer"),
							 TEXT("30s_Mage"),	 TEXT("50s_Dwarf"), TEXT("60s_Elf")};
	const TCHAR *Names[] = {TEXT("고블린"), TEXT("오크"), TEXT("인간"), TEXT("마법사"), TEXT("드워프"), TEXT("엘프")};
	const TCHAR *Categories[] = {TEXT("주류"),	   TEXT("음료"),	 TEXT("신선 식품"),	 TEXT("냉동 식품"),
								 TEXT("물약"),	   TEXT("두루마리"), TEXT("간식"),		 TEXT("생활용품"),
								 TEXT("위생용품"), TEXT("도구"),	 TEXT("숙취 해소제")};
	for (int32 I = 0; I < 6; ++I)
	{
		FCSJournalEntry E;
		E.EntryId = FName(*FString::Printf(TEXT("%s.Introduction"), Races[I]));
		E.Race = static_cast<ECSJournalRace>(I);
		E.Title = FText::FromString(FString::Printf(TEXT("%s의 상품 취향"), Names[I]));
		const FString Path = FString::Printf(
			TEXT("/Game/ConvenienceStore/Data/DataAsset/Customer_Archetypes/DA_Archetype_%s.DA_Archetype_%s"),
			Assets[I], Assets[I]);
		auto *Archetype = LoadObject<UCSCustomerArchetypeDataAsset>(nullptr, *Path);
		FString Body = TEXT("관련 장소를 방문하며 이 종족의 생활과 상품 취향을 조사했다.");
		if (Archetype)
		{
			float Best = -1;
			int32 Category = -1;
			for (const auto &Pair : Archetype->CategoryPreference)
				if (Pair.Value > Best && !Archetype->BlockedCategories.Contains(Pair.Key))
				{
					Best = Pair.Value;
					Category = static_cast<int32>(Pair.Key);
				}
			if (Category >= 0 && Category < UE_ARRAY_COUNT(Categories))
				Body += FString::Printf(TEXT("\n\n상품 중에서는 %s에 가장 높은 관심을 보인다."), Categories[Category]);
			TArray<FString> Blocked;
			for (auto C : Archetype->BlockedCategories)
				if (static_cast<int32>(C) < UE_ARRAY_COUNT(Categories))
					Blocked.Add(Categories[static_cast<int32>(C)]);
			if (!Blocked.IsEmpty())
			{
				Blocked.Sort();
				Body += TEXT("\n\n구매하지 않는 상품 분류: ") + FString::Join(Blocked, TEXT(", ")) + TEXT(".");
			}
		}
		E.Body = FText::FromString(Body);
		Catalog->RaceEntries.Add(E);
	}
}
