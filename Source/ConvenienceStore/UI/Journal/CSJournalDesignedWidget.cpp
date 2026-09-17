#include "UI/Journal/CSJournalDesignedWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/SizeBox.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Data/CSProductTypes.h"

void UCSJournalActionButton::Connect(UCSJournalWidget* InJournal)
{
	OwnerJournal = InJournal;
	OnClicked.AddUniqueDynamic(this, &UCSJournalActionButton::ExecuteAction);
}
void UCSJournalActionButton::ExecuteAction()
{
	if (!OwnerJournal)
		return;
	switch (Action)
	{
	case ECSJournalButtonAction::Page:
		OwnerJournal->SelectPage(static_cast<ECSJournalPage>(Value));
		break;
	case ECSJournalButtonAction::Day:
		OwnerJournal->SelectDay(OwnerJournal->SelectedMonthIndex * 28 + Value);
		break;
	case ECSJournalButtonAction::PreviousMonth:
		OwnerJournal->SelectMonth(OwnerJournal->SelectedMonthIndex - 1);
		break;
	case ECSJournalButtonAction::NextMonth:
		OwnerJournal->SelectMonth(OwnerJournal->SelectedMonthIndex + 1);
		break;
	case ECSJournalButtonAction::Artifact:
		OwnerJournal->SelectArtifact(ArtifactId);
		break;
	}
}
void UCSJournalDesignedWidget::NativeConstruct()
{
	// 디자이너에 배치한 사용자 버튼들을 자동으로 연결합니다.
	if (WidgetTree)
		WidgetTree->ForEachWidget(
			[this](UWidget* Widget)
			{
				if (auto* Button = Cast<UCSJournalActionButton>(Widget))
					Button->Connect(this);
			});
	Super::NativeConstruct();
}
void UCSJournalDesignedWidget::SetLabel(FName Name, const FText& Text)
{
	if (auto* Label = Cast<UTextBlock>(GetWidgetFromName(Name)))
		Label->SetText(Text);
}
void UCSJournalDesignedWidget::SetLabel(FName Name, const FString& Text)
{
	SetLabel(Name, FText::FromString(Text));
}

// 날짜·종족·목록의 텍스트만 변경하므로 디자이너에서 수정한 위치와 스타일은 유지됩니다.
void UCSJournalDesignedWidget::ApplyJournalView()
{
	if (!Journal)
		return;
	const auto Date = GetDateInfo(GetCurrentBusinessDay());
	SetLabel(TEXT("FooterText"), FString::Printf(TEXT("%d / 10 · %d년 %d월 %d일"), static_cast<int32>(SelectedPage) + 1,
												 Date.Year, Date.Month, Date.Day));
	for (int32 I = 0; I < 10; ++I)
	{
		if (auto* Label = Cast<UTextBlock>(GetWidgetFromName(FName(*FString::Printf(TEXT("TabLabel_%d"), I)))))
			Label->SetColorAndOpacity(FSlateColor(I == static_cast<int32>(SelectedPage)
													  ? FLinearColor(.45f, .055f, .015f)
													  : FLinearColor(.018f, .012f, .007f)));
	}
	ECSJournalRace Race;
	if (GetSelectedRace(Race))
	{
		FString Text;
		for (const auto& Entry : GetRaceEntries(Race))
			Text +=
				Entry.Title.ToString() + TEXT("\n\n") +
				(Entry.bUnlocked ? Entry.Body.ToString() : TEXT("이 종족과 관련된 건물을 방문해 상호작용해 주세요.")) +
				TEXT("\n\n\n");
		if (Text.IsEmpty())
			Text = TEXT("아직 등록된 정보가 없습니다.");
		SetLabel(FName(*FString::Printf(TEXT("RaceBody_%d"), static_cast<int32>(Race))), Text);
	}
	if (SelectedPage == ECSJournalPage::Calendar)
	{
		SetLabel(TEXT("MonthText"),
				 FString::Printf(TEXT("%d년 %d월"), SelectedMonthIndex / 12 + 1, SelectedMonthIndex % 12 + 1));
		const auto Selected = GetDateInfo(SelectedBusinessDay);
		SetLabel(TEXT("SelectedDateText"),
				 FString::Printf(TEXT("%d년 %d월 %d일"), Selected.Year, Selected.Month, Selected.Day));
		FString Body;
		for (const auto& Event : GetSelectedDayEvents())
			Body += Event.Title.ToString() + TEXT("\n\n") + Event.Body.ToString() + TEXT("\n\n\n");
		SetLabel(TEXT("EventBodyText"), Body.IsEmpty() ? FString(TEXT("이날의 기록이 없습니다.")) : Body);
		for (const auto& Day : GetCalendarDays())
		{
			const FString Suffix = FString::FromInt(Day.DayOfMonth);
			SetLabel(FName(*(TEXT("DayLabel_") + Suffix)),
					 FString::Printf(TEXT("%d%s\n%s"), Day.DayOfMonth, Day.bToday ? TEXT(" 오늘") : TEXT(""),
									 Day.EventCount ? *FString::Printf(TEXT("%s %d"), TEXT("●"), Day.EventCount)
													: TEXT("")));
			if (auto* Button = Cast<UButton>(GetWidgetFromName(FName(*(TEXT("DayButton_") + Suffix)))))
			{
				Button->SetIsEnabled(Day.bAvailable);
				auto Style = Button->GetStyle();
				Style.Normal.TintColor =
					FSlateColor(Day.bSelected ? FLinearColor::FromSRGBColor(FColor(158, 99, 30))
											  : FLinearColor::FromSRGBColor(FColor(221, 213, 185)));
				Button->SetStyle(Style);
			}
		}
		if (auto* Previous = Cast<UButton>(GetWidgetFromName(TEXT("PreviousMonthButton"))))
			Previous->SetIsEnabled(SelectedMonthIndex > 0);
		if (auto* Next = Cast<UButton>(GetWidgetFromName(TEXT("NextMonthButton"))))
			Next->SetIsEnabled(SelectedMonthIndex < GetLatestMonthIndex());
	}
	if (SelectedPage == ECSJournalPage::Delivery)
		RefreshDeliveries();
	if (SelectedPage == ECSJournalPage::Artifacts)
		RefreshArtifactCells();
}
namespace
{
FString OrderDescription(const FCSDeliveryOrderRuntime& Order)
{
	FString Status;
	switch (Order.Status)
	{
	case ECSDeliveryOrderStatus::Accepted:
		Status = TEXT("진행 중");
		break;
	case ECSDeliveryOrderStatus::Completed:
		Status = TEXT("완료");
		break;
	case ECSDeliveryOrderStatus::Canceled:
		Status = TEXT("취소");
		break;
	case ECSDeliveryOrderStatus::Expired:
		Status = TEXT("기한 초과");
		break;
	default:
		Status = TEXT("실패");
		break;
	}
	FString Result = Order.RecipientDisplayName.ToString() + TEXT(" · ") + Order.RecipientRegionName.ToString() +
					 TEXT("\n") + Status + TEXT("\n");
	for (const auto& Line : Order.RequiredProducts)
	{
		const auto* Product = Line.ProductRow.GetRow<FCSProductDataRow>(TEXT("Journal"));
		Result += FString::Printf(TEXT("%s × %d\n"),
								  Product ? *Product->DisplayName.ToString() : *Line.ProductRow.RowName.ToString(),
								  Line.Quantity);
	}
	Result += FString::Printf(TEXT("%s %d골드 · 평판 %d"),
							  Order.Status == ECSDeliveryOrderStatus::Completed ? TEXT("지급 보상") : TEXT("예정 보상"),
							  Order.RewardGold, Order.ReputationReward);
	return Result;
}
} // namespace
void UCSJournalDesignedWidget::RefreshDeliveries()
{
	FString Active, History;
	for (const auto& Order : GetAcceptedDeliveryOrders())
		Active += OrderDescription(Order) +
				  FString::Printf(TEXT("\n남은 시간: %.0f 게임 분\n\n"), GetRemainingDeliveryMinutes(Order.OrderId));
	for (const auto& Record : GetFinishedDeliveryHistory())
	{
		const auto Start = GetDateInfo(Record.AcceptedDay), End = GetDateInfo(Record.FinishedDay);
		History += FString::Printf(TEXT("%d년 %d월 %d일 수락 → %d년 %d월 %d일 종료\n"), Start.Year, Start.Month,
								   Start.Day, End.Year, End.Month, End.Day) +
				   OrderDescription(Record.Order) + TEXT("\n\n");
	}
	SetLabel(TEXT("ActiveDeliveryText"), Active.IsEmpty() ? FString(TEXT("진행 중인 배달이 없습니다.")) : Active);
	SetLabel(TEXT("DeliveryHistoryText"), History.IsEmpty() ? FString(TEXT("아직 종료된 배달이 없습니다.")) : History);
}
void UCSJournalDesignedWidget::ApplyDeliveryClock()
{
	RefreshDeliveries();
}

// 칸의 디자인은 별도의 WBP_JournalArtifactCell에서 수정하실 수 있습니다.
void UCSJournalDesignedWidget::RefreshArtifactCells()
{
	auto* Grid = Cast<UUniformGridPanel>(GetWidgetFromName(TEXT("ArtifactGrid")));
	if (!Grid)
		return;
	const auto Entries = GetArtifactEntries();
	if (DisplayedCatalog != Journal->Catalog || DisplayedArtifactCount != Entries.Num() ||
		ArtifactCells.Num() != Entries.Num())
	{
		Grid->ClearChildren();
		ArtifactCells.Reset();
		DisplayedCatalog = Journal->Catalog;
		DisplayedArtifactCount = Entries.Num();
		if (ArtifactCellClass)
			for (int32 I = 0; I < Entries.Num(); ++I)
			{
				auto* Cell = CreateWidget<UUserWidget>(GetGameInstance(), ArtifactCellClass);
				if (!Cell)
					continue;
				Grid->AddChildToUniformGrid(Cell, I / 4, I % 4);
				ArtifactCells.Add(Cell);
			}
	}
	for (int32 I = 0; I < ArtifactCells.Num() && I < Entries.Num(); ++I)
	{
		auto* Cell = ArtifactCells[I].Get();
		const auto& Entry = Entries[I];
		if (auto* Label = Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ArtifactNameText"))))
			Label->SetText(Entry.Name);
		if (auto* Icon = Cast<UImage>(Cell->GetWidgetFromName(TEXT("ArtifactIcon"))))
		{
			Icon->SetBrushFromTexture(Entry.Icon);
			Icon->SetColorAndOpacity(Entry.bCollected ? FLinearColor::White : FLinearColor::Black);
		}
		if (auto* Button = Cast<UCSJournalActionButton>(Cell->GetWidgetFromName(TEXT("ArtifactButton"))))
		{
			Button->ArtifactId = Entry.ArtifactId;
			Button->Connect(this);
		}
	}
	FCSJournalArtifactView Selected;
	SetLabel(TEXT("ArtifactDetailText"),
			 GetSelectedArtifact(Selected)
				 ? (Selected.bCollected ? Selected.Name.ToString() + TEXT("\n\n") + Selected.Description.ToString()
										: FString(TEXT("아직 획득하지 못한 아티팩트입니다.")))
				 : FString(TEXT("칸을 선택하면 아티팩트 정보를 확인할 수 있습니다.")));
	if (auto* Empty = GetWidgetFromName(TEXT("ArtifactEmptyText")))
		Empty->SetVisibility(Entries.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

// 지도 배경과 표식은 실제 UMG 위젯이며 플레이어 화살표도 디자이너에서 수정하실 수 있습니다.
void UCSJournalDesignedWidget::ApplyMapView(const FCSJournalMapView& Data)
{
	auto* MapSize = Cast<USizeBox>(GetWidgetFromName(TEXT("MapSizeBox")));
	if (MapSize)
	{
		MapSize->SetWidthOverride(850);
		MapSize->SetHeightOverride(850 * Data.MapSize.Y / FMath::Max(1., Data.MapSize.X));
	}
	if (auto* Image = Cast<UImage>(GetWidgetFromName(TEXT("MapImage"))))
	{
		Image->SetBrushFromTexture(Data.MapTexture);
		Image->SetVisibility(Data.MapTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	auto* Canvas = Cast<UCanvasPanel>(GetWidgetFromName(TEXT("MapMarkers")));
	// 지도 배치가 바뀌었을 때만 표식을 다시 생성하여 주기적인 할당을 줄입니다.
	uint32 LayoutHash = GetTypeHash(Data.MapTexture.Get());
	for (const auto& Shape : Data.Footprints)
	{
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Shape.Position));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Shape.Size));
	}
	for (const auto& Marker : Data.Markers)
	{
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Marker.BuildingName.ToString()));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Marker.Position));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Marker.bImplemented));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Marker.bInBounds));
	}
	if (Canvas && (!bMapLayoutBuilt || MapLayoutHash != LayoutHash))
	{
		bMapLayoutBuilt = true;
		MapLayoutHash = LayoutHash;
		Canvas->ClearChildren();
		if (!Data.MapTexture)
			for (const auto& Shape : Data.Footprints)
			{
				auto* Image = WidgetTree->ConstructWidget<UImage>();
				Image->SetColorAndOpacity(FLinearColor(.23f, .19f, .1f, .3f));
				auto* CanvasSlot = Canvas->AddChildToCanvas(Image);
				CanvasSlot->SetAnchors(FAnchors(Shape.Position.X, Shape.Position.Y, Shape.Position.X + Shape.Size.X,
												Shape.Position.Y + Shape.Size.Y));
				CanvasSlot->SetOffsets(FMargin(0));
			}
		for (const auto& Marker : Data.Markers)
			if (Marker.bInBounds)
			{
				auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
				Text->SetText(FText::FromString(TEXT("■ ") + Marker.BuildingName.ToString()));
				auto Font = Text->GetFont();
				Font.Size = 14;
				Text->SetFont(Font);
				Text->SetColorAndOpacity(Marker.bImplemented ? FLinearColor(.1f, .06f, .02f)
															 : FLinearColor(.3f, .3f, .3f));
				auto* CanvasSlot = Canvas->AddChildToCanvas(Text);
				CanvasSlot->SetAnchors(FAnchors(Marker.Position.X, Marker.Position.Y));
				CanvasSlot->SetPosition(FVector2D::ZeroVector);
				CanvasSlot->SetAutoSize(true);
			}
	}
	if (auto* Arrow = GetWidgetFromName(TEXT("PlayerArrow")))
	{
		Arrow->SetVisibility(Data.bHasPlayer ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		Arrow->SetRenderTransformAngle(Data.PlayerRotationDegrees);
		if (auto* CanvasSlot = Cast<UCanvasPanelSlot>(Arrow->Slot))
		{
			CanvasSlot->SetAnchors(FAnchors(Data.PlayerPosition.X, Data.PlayerPosition.Y));
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
		}
	}
}
