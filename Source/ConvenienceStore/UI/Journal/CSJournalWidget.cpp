#include "UI/Journal/CSJournalWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Features/Journal/CSJournalWorldActors.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

namespace
{
constexpr int32 DaysPerMonth = 28;
constexpr int32 PageCount = 10;
bool IsInMap(FVector2D Point)
{
	return Point.X >= 0 && Point.X <= 1 && Point.Y >= 0 && Point.Y <= 1;
}
FVector2D ClampMapPoint(FVector2D Point)
{
	return FVector2D(FMath::Clamp(Point.X, 0., 1.), FMath::Clamp(Point.Y, 0., 1.));
}
} // namespace

// WBP 디자이너에서 제작한 화면 구성을 유지하며 데이터와 버튼 연결만 초기화합니다.
void UCSJournalWidget::NativeConstruct()
{
	SetIsFocusable(true);
	Super::NativeConstruct();
	if (auto* GI = GetGameInstance())
		Journal = GI->GetSubsystem<UCSJournalSubsystem>();
	if (!Journal)
		return;
	CacheMapGeometry();
	Journal->ObserveDay(GetCurrentBusinessDay());
	SelectedBusinessDay = GetCurrentBusinessDay();
	SelectedMonthIndex = (SelectedBusinessDay - 1) / DaysPerMonth;
	Journal->OnJournalChanged.AddUniqueDynamic(this, &UCSJournalWidget::RefreshJournal);
	if (CloseButton)
		CloseButton->OnClicked.AddUniqueDynamic(this, &UCSJournalWidget::RequestClose);
	if (NextPageButton)
		NextPageButton->OnClicked.AddUniqueDynamic(this, &UCSJournalWidget::NextPage);
	if (PreviousPageButton)
		PreviousPageButton->OnClicked.AddUniqueDynamic(this, &UCSJournalWidget::PreviousPage);
	RefreshJournal();
	OnJournalPageChanged(SelectedPage);
	UpdateLiveData();
	GetWorld()->GetTimerManager().SetTimer(LiveUpdateTimer, this, &UCSJournalWidget::UpdateLiveData,
										   FMath::Max(0.05f, LiveUpdateInterval), true);
}

// 닫힌 화면에 갱신 요청이 전달되지 않도록 알림 구독, 타이머와 버튼 연결을 해제합니다.
void UCSJournalWidget::NativeDestruct()
{
	if (GetWorld())
		GetWorld()->GetTimerManager().ClearTimer(LiveUpdateTimer);
	if (Journal)
		Journal->OnJournalChanged.RemoveDynamic(this, &UCSJournalWidget::RefreshJournal);
	if (CloseButton)
		CloseButton->OnClicked.RemoveDynamic(this, &UCSJournalWidget::RequestClose);
	if (NextPageButton)
		NextPageButton->OnClicked.RemoveDynamic(this, &UCSJournalWidget::NextPage);
	if (PreviousPageButton)
		PreviousPageButton->OnClicked.RemoveDynamic(this, &UCSJournalWidget::PreviousPage);
	Super::NativeDestruct();
}

// 자식 버튼에 포커스가 있어도 Tab과 Esc를 먼저 처리하여 수첩을 닫습니다.
FReply UCSJournalWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Tab || Event.GetKey() == EKeys::Escape)
	{
		RequestClose();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
void UCSJournalWidget::RequestClose()
{
	if (auto* Controller = Cast<ACSStorePlayerController>(GetOwningPlayer()))
		Controller->CloseJournal();
}
void UCSJournalWidget::SelectPage(ECSJournalPage NewPage)
{
	if (static_cast<int32>(NewPage) >= PageCount)
		return;
	const bool bChanged = SelectedPage != NewPage;
	SelectedPage = NewPage;
	if (Journal && NewPage == ECSJournalPage::Calendar)
		Journal->MarkDayRead(SelectedBusinessDay);
	RefreshJournal();
	if (bChanged)
		OnJournalPageChanged(SelectedPage);
	UpdateLiveData();
	SetKeyboardFocus();
}
void UCSJournalWidget::NextPage()
{
	SelectPage(static_cast<ECSJournalPage>(FMath::Min(PageCount - 1, static_cast<int32>(SelectedPage) + 1)));
}
void UCSJournalWidget::PreviousPage()
{
	SelectPage(static_cast<ECSJournalPage>(FMath::Max(0, static_cast<int32>(SelectedPage) - 1)));
}
bool UCSJournalWidget::SelectMonth(int32 NewMonthIndex)
{
	if (!Journal || NewMonthIndex < 0 || NewMonthIndex > GetLatestMonthIndex())
		return false;
	SelectedMonthIndex = NewMonthIndex;
	SelectedBusinessDay = NewMonthIndex * DaysPerMonth + 1;
	if (SelectedPage == ECSJournalPage::Calendar)
		Journal->MarkDayRead(SelectedBusinessDay);
	RefreshJournal();
	SetKeyboardFocus();
	return true;
}
bool UCSJournalWidget::SelectDay(int32 BusinessDay)
{
	if (!Journal || BusinessDay < 1 || BusinessDay > Journal->LatestDay)
		return false;
	SelectedBusinessDay = BusinessDay;
	SelectedMonthIndex = (BusinessDay - 1) / DaysPerMonth;
	if (SelectedPage == ECSJournalPage::Calendar)
		Journal->MarkDayRead(BusinessDay);
	RefreshJournal();
	SetKeyboardFocus();
	return true;
}
bool UCSJournalWidget::SelectArtifact(FName ArtifactId)
{
	if (!Journal || !Journal->Catalog ||
		!Journal->Catalog->Artifacts.ContainsByPredicate([&](const FCSJournalArtifact& A)
														 { return A.ArtifactId == ArtifactId; }))
		return false;
	SelectedArtifactId = ArtifactId;
	RefreshJournal();
	SetKeyboardFocus();
	return true;
}

// WBP의 자식 위젯을 다시 생성하지 않고 갱신 이벤트로 필요한 표시만 변경합니다.
void UCSJournalWidget::RefreshJournal()
{
	if (!Journal || bRefreshing)
		return;
	TGuardValue<bool> Guard(bRefreshing, true);
	Journal->ObserveDay(GetCurrentBusinessDay());
	SelectedMonthIndex = FMath::Clamp(SelectedMonthIndex, 0, GetLatestMonthIndex());
	SelectedBusinessDay = FMath::Clamp(SelectedBusinessDay, SelectedMonthIndex * DaysPerMonth + 1,
									   FMath::Min(Journal->LatestDay, (SelectedMonthIndex + 1) * DaysPerMonth));
	UpdateNavigationWidgets();
	ApplyJournalView();
	OnJournalUpdated();
}
void UCSJournalWidget::UpdateNavigationWidgets()
{
	const int32 Index = static_cast<int32>(SelectedPage);
	if (PageSwitcher && Index < PageSwitcher->GetNumWidgets())
		PageSwitcher->SetActiveWidgetIndex(Index);
	if (PageTitleText)
		PageTitleText->SetText(GetPageTitle());
	if (PreviousPageButton)
		PreviousPageButton->SetIsEnabled(Index > 0);
	if (NextPageButton)
		NextPageButton->SetIsEnabled(Index < PageCount - 1);
}

// 선택한 구역에만 주기적인 갱신을 알리며 목록 재구성과 화면 전체의 Tick을 줄입니다.
void UCSJournalWidget::UpdateLiveData()
{
	if (SelectedPage == ECSJournalPage::Map)
	{
		const auto Data = GetMapData();
		ApplyMapView(Data);
		OnJournalMapUpdated(Data);
	}
	else if (SelectedPage == ECSJournalPage::Delivery)
	{
		const auto* State = GetWorld()->GetGameState<ACSStoreGameState>();
		ApplyDeliveryClock();
		OnJournalDeliveryClockUpdated(State ? State->CurrentGameMinutes : 0.f);
	}
}
FText UCSJournalWidget::GetPageTitle() const
{
	const TCHAR* Names[] = {TEXT("지도"),	   TEXT("고블린"),		 TEXT("오크"), TEXT("모험가"),
							TEXT("마법사"),	   TEXT("드워프"),		 TEXT("엘프"), TEXT("달력 · 사건 기록"),
							TEXT("배달 기록"), TEXT("아티팩트 도감")};
	return FText::FromString(Names[FMath::Clamp(static_cast<int32>(SelectedPage), 0, PageCount - 1)]);
}
bool UCSJournalWidget::GetSelectedRace(ECSJournalRace& OutRace) const
{
	const int32 Index = static_cast<int32>(SelectedPage) - 1;
	OutRace = ECSJournalRace::Goblin;
	if (Index < 0 || Index > 5)
		return false;
	OutRace = static_cast<ECSJournalRace>(Index);
	return true;
}
int32 UCSJournalWidget::GetCurrentBusinessDay() const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	return State ? FMath::Max(1, State->CurrentBusinessDay) : 1;
}
int32 UCSJournalWidget::GetLatestMonthIndex() const
{
	return Journal ? (FMath::Max(1, Journal->LatestDay) - 1) / DaysPerMonth : 0;
}
FCSJournalDateView UCSJournalWidget::GetDateInfo(int32 BusinessDay)
{
	FCSJournalDateView Date;
	const int32 Index = FMath::Max(1, BusinessDay) - 1;
	Date.MonthIndex = Index / DaysPerMonth;
	Date.Year = Date.MonthIndex / 12 + 1;
	Date.Month = Date.MonthIndex % 12 + 1;
	Date.Day = Index % DaysPerMonth + 1;
	return Date;
}
TArray<FCSJournalCalendarDayView> UCSJournalWidget::GetCalendarDays() const
{
	TArray<FCSJournalCalendarDayView> Days;
	for (int32 I = 0; I < DaysPerMonth; ++I)
	{
		FCSJournalCalendarDayView D;
		D.DayOfMonth = I + 1;
		D.BusinessDay = SelectedMonthIndex * DaysPerMonth + I + 1;
		D.bToday = D.BusinessDay == GetCurrentBusinessDay();
		D.bSelected = D.BusinessDay == SelectedBusinessDay;
		D.bAvailable = Journal && D.BusinessDay <= Journal->LatestDay;
		if (Journal)
			for (const auto& Event : Journal->Events)
				if (Event.BusinessDay == D.BusinessDay)
				{
					++D.EventCount;
					D.bUnread |= !Event.bRead;
				}
		Days.Add(D);
	}
	return Days;
}
TArray<FCSJournalEvent> UCSJournalWidget::GetSelectedDayEvents() const
{
	return Journal ? Journal->Events.FilterByPredicate([this](const FCSJournalEvent& E)
													   { return E.BusinessDay == SelectedBusinessDay; })
				   : TArray<FCSJournalEvent>();
}
TArray<FCSJournalRaceEntryView> UCSJournalWidget::GetRaceEntries(ECSJournalRace Race) const
{
	TArray<FCSJournalRaceEntryView> Entries;
	if (Journal && Journal->Catalog)
		for (const auto& Entry : Journal->Catalog->RaceEntries)
			if (Entry.Race == Race)
			{
				FCSJournalRaceEntryView View;
				View.EntryId = Entry.EntryId;
				View.Race = Race;
				View.bUnlocked = Journal->IsEntryUnlocked(Entry.EntryId);
				View.Title = View.bUnlocked ? Entry.Title : FText::FromString(TEXT("아직 발견하지 못한 기록"));
				if (View.bUnlocked)
					View.Body = Entry.Body;
				Entries.Add(View);
			}
	return Entries;
}
TArray<FCSJournalArtifactView> UCSJournalWidget::GetArtifactEntries() const
{
	TArray<FCSJournalArtifactView> Entries;
	if (Journal && Journal->Catalog)
		for (const auto& Artifact : Journal->Catalog->Artifacts)
		{
			FCSJournalArtifactView View;
			View.ArtifactId = Artifact.ArtifactId;
			View.Icon = Artifact.Icon;
			View.bCollected = Journal->IsArtifactCollected(Artifact.ArtifactId);
			View.Name = View.bCollected ? Artifact.Name : FText::FromString(TEXT("???"));
			if (View.bCollected)
				View.Description = Artifact.Description;
			Entries.Add(View);
		}
	return Entries;
}
bool UCSJournalWidget::GetSelectedArtifact(FCSJournalArtifactView& OutArtifact) const
{
	OutArtifact = FCSJournalArtifactView();
	for (const auto& A : GetArtifactEntries())
		if (A.ArtifactId == SelectedArtifactId)
		{
			OutArtifact = A;
			return true;
		}
	return false;
}
TArray<FCSDeliveryOrderRuntime> UCSJournalWidget::GetAcceptedDeliveryOrders() const
{
	auto* Orders = GetWorld() ? GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>() : nullptr;
	return Orders ? Orders->GetDeliveryOrders().FilterByPredicate(
						[](const FCSDeliveryOrderRuntime& O) { return O.Status == ECSDeliveryOrderStatus::Accepted; })
				  : TArray<FCSDeliveryOrderRuntime>();
}
TArray<FCSJournalDelivery> UCSJournalWidget::GetFinishedDeliveryHistory() const
{
	TArray<FCSJournalDelivery> Result;
	if (Journal)
		for (int32 I = Journal->Deliveries.Num() - 1; I >= 0; --I)
			if (Journal->Deliveries[I].FinishedDay > 0)
				Result.Add(Journal->Deliveries[I]);
	return Result;
}
float UCSJournalWidget::GetRemainingDeliveryMinutes(int32 OrderId) const
{
	auto* Orders = GetWorld() ? GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>() : nullptr;
	const auto* State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	FCSDeliveryOrderRuntime Order;
	if (!Orders || !State || !Orders->GetDeliveryOrderById(OrderId, Order) ||
		Order.Status != ECSDeliveryOrderStatus::Accepted)
		return 0;
	return FMath::Max(0.f, Order.DeliveryDeadlineMinutes - State->CurrentGameMinutes);
}

// 레벨의 정적인 형상은 수첩을 열 때 계산하고 플레이어 위치는 실시간으로 조회합니다.
void UCSJournalWidget::CacheMapGeometry()
{
	CachedMap = FCSJournalMapView();
	MapBounds = FBox(ForceInit);
	MapSettings = nullptr;
	if (!GetWorld())
		return;
	for (TActorIterator<ACSJournalMapSettings> It(GetWorld()); It; ++It)
	{
		MapSettings = *It;
		break;
	}
	TArray<FBox> Boxes;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<ACSJournalDiscoveryPoint>())
			continue;
		TInlineComponentArray<UStaticMeshComponent*> Meshes;
		It->GetComponents(Meshes);
		for (auto* Mesh : Meshes)
			if (Mesh->IsVisible() && Mesh->GetStaticMesh() && Mesh->Bounds.BoxExtent.GetMax() < 50000)
			{
				Boxes.Add(Mesh->Bounds.GetBox());
				MapBounds += Mesh->Bounds.GetBox();
			}
	}
	for (TActorIterator<ACSJournalMapMarker> It(GetWorld()); It; ++It)
		MapBounds += It->GetActorLocation();
	if (GetOwningPlayer() && GetOwningPlayer()->GetPawn())
		MapBounds += GetOwningPlayer()->GetPawn()->GetActorLocation();
	if (!MapBounds.IsValid || MapBounds.GetSize().X < 100 || MapBounds.GetSize().Y < 100)
		MapBounds = FBox(FVector(-10000, -10000, -1000), FVector(10000, 10000, 1000));
	MapBounds = MapBounds.ExpandBy(200);
	CachedMap.MapSize = IsValid(MapSettings)
							? FVector2D(FMath::Max(1., MapSettings->MapSize.X), FMath::Max(1., MapSettings->MapSize.Y))
							: FVector2D(MapBounds.GetSize().X, MapBounds.GetSize().Y);
	CachedMap.MapTexture = IsValid(MapSettings) ? MapSettings->MapTexture : nullptr;
	Boxes.Sort([](const FBox& A, const FBox& B)
			   { return A.GetSize().X * A.GetSize().Y > B.GetSize().X * B.GetSize().Y; });
	if (!CachedMap.MapTexture)
		for (const auto& Box : Boxes)
		{
			FVector2D Min(1e9, 1e9), Max(-1e9, -1e9);
			for (int32 X = 0; X < 2; ++X)
				for (int32 Y = 0; Y < 2; ++Y)
				{
					const auto P = WorldToMap(FVector(X ? Box.Max.X : Box.Min.X, Y ? Box.Max.Y : Box.Min.Y, 0));
					Min.X = FMath::Min(Min.X, P.X);
					Min.Y = FMath::Min(Min.Y, P.Y);
					Max.X = FMath::Max(Max.X, P.X);
					Max.Y = FMath::Max(Max.Y, P.Y);
				}
			Min = ClampMapPoint(Min);
			Max = ClampMapPoint(Max);
			FCSJournalMapFootprintView View;
			View.Position = Min;
			View.Size = Max - Min;
			CachedMap.Footprints.Add(View);
		}
}
FVector2D UCSJournalWidget::WorldToMap(FVector Position) const
{
	if (IsValid(MapSettings))
	{
		const auto Local = MapSettings->GetActorTransform().InverseTransformPosition(Position);
		return FVector2D(Local.X / FMath::Max(1., MapSettings->MapSize.X) + .5,
						 .5 - Local.Y / FMath::Max(1., MapSettings->MapSize.Y));
	}
	return FVector2D((Position.X - MapBounds.Min.X) / FMath::Max(1., MapBounds.GetSize().X),
					 (MapBounds.Max.Y - Position.Y) / FMath::Max(1., MapBounds.GetSize().Y));
}
FCSJournalMapView UCSJournalWidget::GetMapData() const
{
	FCSJournalMapView View = CachedMap;
	if (!GetWorld())
		return View;
	for (TActorIterator<ACSJournalMapMarker> It(GetWorld()); It; ++It)
	{
		FCSJournalMapMarkerView Marker;
		Marker.BuildingName = It->BuildingName;
		Marker.Race = It->Race;
		Marker.bImplemented = It->bImplemented;
		Marker.Position = WorldToMap(It->GetActorLocation());
		Marker.bInBounds = IsInMap(Marker.Position);
		View.Markers.Add(Marker);
	}
	const APawn* Pawn = GetOwningPlayer() ? GetOwningPlayer()->GetPawn() : nullptr;
	if (Pawn)
	{
		View.bHasPlayer = true;
		const auto Point = WorldToMap(Pawn->GetActorLocation());
		View.bPlayerInBounds = IsInMap(Point);
		View.PlayerPosition = ClampMapPoint(Point);
		View.PlayerRotationDegrees =
			-(Pawn->GetActorRotation().Yaw - (IsValid(MapSettings) ? MapSettings->GetActorRotation().Yaw : 0.f));
	}
	return View;
}
