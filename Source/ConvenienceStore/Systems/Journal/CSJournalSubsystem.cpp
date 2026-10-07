#include "Systems/Journal/CSJournalSubsystem.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Data/CSJournalSettings.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/SaveLoad/CSSaveGame.h"
#include "Systems/SaveLoad/CSSaveRecordUtils.h"

void UCSJournalSubsystem::CaptureSaveData(UCSSaveGame& OutSave) const
{
	FCSJournalSaveRecord& Record = OutSave.Journal;
	Record.UnlockedEntries = UnlockedEntries.Array();
	Record.CollectedArtifacts = CollectedArtifacts.Array();
	Record.LatestDay = LatestDay;

	Record.Events.Reset(Events.Num());
	for (const FCSJournalEvent& Event : Events)
	{
		Record.Events.Add(CSSaveRecordUtils::ToRecord(Event));
	}

	Record.Deliveries.Reset(Deliveries.Num());
	for (const FCSJournalDelivery& Delivery : Deliveries)
	{
		Record.Deliveries.Add(CSSaveRecordUtils::ToRecord(Delivery));
	}
}

void UCSJournalSubsystem::RestoreSaveData(const UCSSaveGame& InSave)
{
	const FCSJournalSaveRecord& Record = InSave.Journal;

	// 레벨 전환 후에도 유지되는 서브시스템이므로 이전 세션 기록이 섞이지 않도록 먼저 비움
	UnlockedEntries.Reset();
	CollectedArtifacts.Reset();
	Events.Reset();
	Deliveries.Reset();

	UnlockedEntries.Append(Record.UnlockedEntries);
	CollectedArtifacts.Append(Record.CollectedArtifacts);
	LatestDay = FMath::Max(1, Record.LatestDay);

	Events.Reserve(Record.Events.Num());
	for (const FCSJournalEventRecord& EventRecord : Record.Events)
	{
		Events.Add(CSSaveRecordUtils::ToRuntime(EventRecord));
	}

	Deliveries.Reserve(Record.Deliveries.Num());
	for (const FCSJournalDeliveryRecord& DeliveryRecord : Record.Deliveries)
	{
		Deliveries.Add(CSSaveRecordUtils::ToRuntime(DeliveryRecord));
	}

	OnJournalChanged.Broadcast();
}

void UCSJournalSubsystem::ResetJournal()
{
	if (UCSArtifactSubsystem* Artifacts = GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>()) Artifacts->ResetArtifacts();
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

void UCSJournalSubsystem::RegisterArtifactTable(UDataTable* Table)
{
	if (!IsValid(Table) || !Table->GetRowStruct() ||
		!Table->GetRowStruct()->IsChildOf(FCSJournalArtifactDataRow::StaticStruct()) ||
		ArtifactTables.Contains(Table))
	{
		return;
	}
	ArtifactTables.Add(Table);
	OnJournalChanged.Broadcast();
}

TArray<FCSJournalArtifact> UCSJournalSubsystem::GetAllArtifacts() const
{
	TArray<FCSJournalArtifact> Result = Catalog ? Catalog->Artifacts : TArray<FCSJournalArtifact>();
	for (const UDataTable* Table : ArtifactTables)
	{
		if (!IsValid(Table))
		{
			continue;
		}
		TArray<FName> RowNames = Table->GetRowNames();
		RowNames.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
		for (const FName RowName : RowNames)
		{
			const FCSJournalArtifactDataRow* Row = Table->FindRow<FCSJournalArtifactDataRow>(RowName, TEXT("UCSJournalSubsystem::GetAllArtifacts"));
			if (!Row)
			{
				continue;
			}
			FCSJournalArtifact Artifact;
			Artifact.ArtifactId = RowName;
			Artifact.Name = Row->DisplayName;
			Artifact.Description = Row->Description;
			Artifact.Icon = Row->Icon;
			if (FCSJournalArtifact* Existing = Result.FindByPredicate(
				[&](const FCSJournalArtifact& Entry) { return Entry.ArtifactId == RowName; }))
			{
				// 테이블에서 아직 채우지 않은 표시 항목은 기존 도감 에셋 값을 유지합니다.
				if (!Artifact.Name.IsEmpty()) Existing->Name = Artifact.Name;
				if (!Artifact.Description.IsEmpty()) Existing->Description = Artifact.Description;
				if (Artifact.Icon) Existing->Icon = Artifact.Icon;
			}
			else
			{
				Result.Add(Artifact);
			}
		}
	}
	return Result;
}

bool UCSJournalSubsystem::FindArtifact(FName ArtifactId, FCSJournalArtifact& OutArtifact) const
{
	for (const FCSJournalArtifact& Artifact : GetAllArtifacts())
	{
		if (Artifact.ArtifactId == ArtifactId)
		{
			OutArtifact = Artifact;
			return true;
		}
	}
	return false;
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
	UCSJournalCatalog* ConfiguredCatalog = GetDefault<UCSJournalSettings>()->Catalog.LoadSynchronous();
	if (IsValid(ConfiguredCatalog) && !ConfiguredCatalog->RaceEntries.IsEmpty())
	{
		Catalog = ConfiguredCatalog;
		return;
	}

	// 공용 에셋에 내용이 아직 없을 때에도 기존 게임 진행을 유지하는 초기값입니다.
	Catalog = NewObject<UCSJournalCatalog>(this);
	const TCHAR *Races[] = {TEXT("Goblin"), TEXT("Orc"), TEXT("Human"), TEXT("Wizard"), TEXT("Dwarf"), TEXT("Elf")};
	const TCHAR *Names[] = {TEXT("고블린"), TEXT("오크"), TEXT("인간"), TEXT("마법사"), TEXT("드워프"), TEXT("엘프")};
	const TCHAR *LifeDescriptions[] = {
		TEXT("고블린은 작고 민첩하며 실용적인 생활을 한다. 값비싼 물건보다는 당장 사용할 수 있는 물건을 선호하며, 시장이나 상점 근처에서 자주 모습을 드러낸다. 새로운 먹거리와 눈에 띄는 상품에도 관심이 많다."),
		TEXT("오크는 강인한 체력을 바탕으로 육체적인 일을 하며 생활한다. 복잡한 장식이나 격식보다는 튼튼하고 실용적인 물건을 중요하게 생각한다. 일과를 마친 뒤에는 충분한 식사와 휴식을 즐긴다."),
		TEXT("모험가는 여러 지역을 돌아다니며 의뢰를 수행하고 새로운 장소를 탐험한다. 언제 다시 길을 떠날지 모르기 때문에 식량과 회복용품, 여행에 필요한 물건을 미리 준비하는 편이다."),
		TEXT("마법사는 마법을 연구하거나 새로운 주문을 익히며 많은 시간을 보낸다. 연구에 몰두하면 식사나 휴식을 잊기도 하며, 마법 실험에 사용할 물약과 두루마리 같은 물건에 익숙하다."),
		TEXT("드워프는 광산과 공방에서 일하며 물건을 만들고 수리하는 생활에 익숙하다. 오래 사용할 수 있는 튼튼한 물건을 높게 평가하며, 일을 마친 뒤 동료들과 함께 쉬는 시간을 중요하게 여긴다."),
		TEXT("엘프는 자연과 조화를 이루며 차분한 생활을 한다. 주변 환경을 세심하게 관리하고 깨끗한 상태를 유지하는 것을 중요하게 생각한다. 인공적인 물건보다는 신선하고 자연에 가까운 상품에 관심을 보인다.")};
	const TCHAR *Categories[] = {TEXT("주류"),	   TEXT("음료"),	 TEXT("신선 식품"),	 TEXT("냉동 식품"),
								 TEXT("물약"),	   TEXT("두루마리"), TEXT("간식"),		 TEXT("생활용품"),
								 TEXT("위생용품"), TEXT("도구"),	 TEXT("숙취 해소제")};
	for (int32 I = 0; I < 6; ++I)
	{
		FCSJournalEntry E;
		E.EntryId = FName(*FString::Printf(TEXT("%s.Introduction"), Races[I]));
		E.Race = static_cast<ECSJournalRace>(I);
		E.Title = FText::FromString(FString::Printf(TEXT("%s의 상품 취향"), Names[I]));
		const UCSCustomerArchetypeDataAsset* Archetype = IsValid(ConfiguredCatalog)
			? ConfiguredCatalog->FallbackArchetypes.FindRef(E.Race).Get() : nullptr;
		E.LifeBody = FText::FromString(LifeDescriptions[I]);
		FString PreferenceBody = TEXT("관련 장소를 방문하며 이 종족의 생활과 상품 취향을 조사했다.");
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
				PreferenceBody += FString::Printf(TEXT("\n\n상품 중에서는 %s에 가장 높은 관심을 보인다."), Categories[Category]);
			TArray<FString> Blocked;
			for (auto C : Archetype->BlockedCategories)
				if (static_cast<int32>(C) < UE_ARRAY_COUNT(Categories))
					Blocked.Add(Categories[static_cast<int32>(C)]);
			if (!Blocked.IsEmpty())
			{
				Blocked.Sort();
				if (!PreferenceBody.IsEmpty()) PreferenceBody += TEXT("\n\n");
				PreferenceBody += TEXT("구매하지 않는 상품 분류: ") + FString::Join(Blocked, TEXT(", ")) + TEXT(".");
			}
		}
		E.PreferenceBody = FText::FromString(PreferenceBody);
		E.Body = FText::FromString(FString(LifeDescriptions[I]) + TEXT("\n\n") + PreferenceBody);
		Catalog->RaceEntries.Add(E);
	}
}
