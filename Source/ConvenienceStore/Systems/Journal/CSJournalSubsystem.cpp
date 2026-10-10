#include "Systems/Journal/CSJournalSubsystem.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Systems/Audio/CSAudioSubsystem.h"
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
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::JournalEntryAdded);
	return true;
}
// 새 아티팩트를 획득한 경우에만 수집 상태를 변경하고 도감 화면에 알립니다.
bool UCSJournalSubsystem::CollectArtifact(FName Id)
{
	if (Id.IsNone() || CollectedArtifacts.Contains(Id))
		return false;
	CollectedArtifacts.Add(Id);
	OnJournalChanged.Broadcast();
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::JournalEntryAdded);
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
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::JournalEntryAdded);
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
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::JournalEntryAdded);
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
	const TCHAR *Names[] = {TEXT("고블린"), TEXT("오크"), TEXT("모험가"), TEXT("마법사"), TEXT("드워프"), TEXT("엘프")};
	// 수첩 위젯의 GetRaceLifeDescription과 같은 글이다. 한쪽을 고치면 다른 쪽도 고쳐야 한다
	const TCHAR *LifeDescriptions[] = {
		TEXT("고블린 학교 꼬마들은 수업이 끝나면 공부 석판을 끼고 가게로 몰려온다. 바삭칩 같은 간식과 아이스크림, 달콤한 음료를 고르지만 술은 아직 못 산다."),
		TEXT("오크들은 일을 마치면 오크 주점에서 엄니 축배잔을 부딪친다. 술과 고향 만두 같은 냉동식품을 챙기고, 술을 산 날엔 숙취해소제도 잊지 않는다."),
		TEXT("모험가들은 길을 떠나기 전 포션 상점에서 여정의 포션 수통을 채운다. 회복 포션과 횃불 같은 여행 용품, 길에서 먹을 도시락을 챙겨 간다."),
		TEXT("마법사들은 마법 교습소에서 주문을 익히고, 밤이면 별자리 관측구에서 하늘을 읽는다. 포션과 두루마리, 밤샘을 버틸 음료를 꾸준히 찾는다."),
		TEXT("드워프들은 공방에서 팔각 각인 망치를 두드리며 하루를 보낸다. 곡괭이 같은 튼튼한 도구를 고르는 눈이 까다롭고, 일을 마치면 흑맥주 같은 술을 즐긴다."),
		TEXT("엘프들은 검술 수련장에서 숲지기 수련검으로 수련한다. 수련 뒤에는 허브 비누로 씻고, 신선한 음식과 찬이슬 같은 술로 하루를 마무리한다.")};
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
			// 주력 선호는 보통 2~3개가 같은 값이라 최고값과 같은 카테고리를 전부 적는다
			float Best = -1;
			for (const auto &Pair : Archetype->CategoryPreference)
				if (!Archetype->BlockedCategories.Contains(Pair.Key))
					Best = FMath::Max(Best, Pair.Value);
			TArray<int32> BestIndices;
			for (const auto &Pair : Archetype->CategoryPreference)
			{
				const int32 Index = static_cast<int32>(Pair.Key);
				if (FMath::IsNearlyEqual(Pair.Value, Best) && !Archetype->BlockedCategories.Contains(Pair.Key))
					BestIndices.Add(Index);
			}
			BestIndices.Sort();
			TArray<FString> BestNames;
			for (const int32 Index : BestIndices)
				BestNames.Add(CSGetProductCategoryDisplayName(static_cast<ECSProductCategory>(Index)).ToString());
			if (!BestNames.IsEmpty())
				PreferenceBody += FString::Printf(TEXT("\n\n상품 중에서는 %s에 가장 높은 관심을 보인다."), *FString::Join(BestNames, TEXT(", ")));
			TArray<FString> Blocked;
			for (auto C : Archetype->BlockedCategories)
				Blocked.Add(CSGetProductCategoryDisplayName(C).ToString());
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
