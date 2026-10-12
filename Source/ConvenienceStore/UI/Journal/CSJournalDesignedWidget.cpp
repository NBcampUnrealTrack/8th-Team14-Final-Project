#include "UI/Journal/CSJournalDesignedWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/Weather/CSWeatherSubsystem.h"
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
	case ECSJournalButtonAction::UseArtifact:
		OwnerJournal->UseSelectedArtifact();
		break;
	case ECSJournalButtonAction::CancelArtifactUse:
		OwnerJournal->CancelArtifactUse();
		break;
	}
}
void UCSJournalArtifactClickProxy::Connect(UCSJournalWidget* InJournal, FName InArtifactId)
{
	OwnerJournal = InJournal;
	ArtifactId = InArtifactId;
}
void UCSJournalArtifactClickProxy::Execute()
{
	if (OwnerJournal)
		OwnerJournal->SelectArtifact(ArtifactId);
}
void UCSJournalDesignedWidget::NativeConstruct()
{
	// UMG와 Blueprint의 Construct가 끝난 뒤 버튼 델리게이트를 연결합니다.
	Super::NativeConstruct();
	// 사용 버튼은 디자이너에 배치한 위젯만 사용합니다. 런타임에 자동 생성하지 않습니다.
	ArtifactUseButton = Cast<UCSJournalActionButton>(GetWidgetFromName(TEXT("ArtifactDetailUseButton")));
	if (ArtifactUseButton)
		ArtifactUseButton->Action = ECSJournalButtonAction::UseArtifact;
	ArtifactCancelUseButton = Cast<UCSJournalActionButton>(GetWidgetFromName(TEXT("ArtifactDetailCancelUseButton")));
	if (ArtifactCancelUseButton)
		ArtifactCancelUseButton->Action = ECSJournalButtonAction::CancelArtifactUse;
	// 디자이너에 배치한 사용자 버튼들을 자동으로 연결합니다.
	if (WidgetTree)
		WidgetTree->ForEachWidget(
			[this](UWidget* Widget)
			{
				if (auto* Button = Cast<UCSJournalActionButton>(Widget))
				{
					const FString ButtonName = Button->GetName();
					if (ButtonName.StartsWith(TEXT("TabButton_")))
					{
						int32 PageIndex = INDEX_NONE;
						if (LexTryParseString(PageIndex, *ButtonName.RightChop(10)) &&
							PageIndex >= 0 && PageIndex < 10)
						{
							Button->Action = ECSJournalButtonAction::Page;
							Button->Value = PageIndex;
						}
					}
					Button->Connect(this);
				}
			});
	// Super 호출 중 한 초기 표시 이후, 동적 위젯까지 포함한 상태를 최종 반영합니다.
	RefreshJournal();
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

namespace
{
FName GetRaceDetailLabelName(const TCHAR* BaseName, ECSJournalRace Race)
{
	// WBP_Journal에서 엘프 상세 텍스트만 접미사 없는 원래 이름을 사용합니다.
	if (Race == ECSJournalRace::Elf)
		return FName(BaseName);
	return FName(*FString::Printf(TEXT("%s_%d"), BaseName, static_cast<int32>(Race)));
}
}

void UCSJournalDesignedWidget::RefreshCalendarEvents()
{
	const TArray<FCSJournalEvent> Events = GetSelectedDayEvents();
	FString NewsTitle;
	FString NewsBody;
	FString HolidayTitle;
	FString HolidayBody;
	for (const FCSJournalEvent& Event : Events)
	{
		const bool bHoliday = Event.EventId.ToString().StartsWith(TEXT("Holiday."));
		FString& TargetTitle = bHoliday ? HolidayTitle : NewsTitle;
		FString& TargetBody = bHoliday ? HolidayBody : NewsBody;
		if (!TargetTitle.IsEmpty()) TargetTitle += TEXT("\n\n");
		if (!TargetBody.IsEmpty()) TargetBody += TEXT("\n\n");
		TargetTitle += Event.Title.ToString();
		TargetBody += Event.Body.ToString();
	}

	SetLabel(TEXT("NewsTitleText"), NewsTitle);
	SetLabel(TEXT("NewsBodyText"), NewsBody);
	SetLabel(TEXT("HolidayTitleText"), HolidayTitle);
	SetLabel(TEXT("HolidayBodyText"), HolidayBody);
	if (UWidget* Section = GetWidgetFromName(TEXT("NewsSection")))
		Section->SetVisibility(NewsTitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (UWidget* Section = GetWidgetFromName(TEXT("HolidaySection")))
		Section->SetVisibility(HolidayTitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (UWidget* EmptyText = GetWidgetFromName(TEXT("EventEmptyText")))
		EmptyText->SetVisibility(Events.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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
		const bool bSelected = I == static_cast<int32>(SelectedPage);
		if (auto* Label = Cast<UTextBlock>(GetWidgetFromName(FName(*FString::Printf(TEXT("TabLabel_%d"), I)))))
			Label->SetColorAndOpacity(FSlateColor(bSelected ? SelectedTabTextColor : DefaultTabTextColor));
		if (auto* SelectedImage = Cast<UImage>(GetWidgetFromName(
			FName(*FString::Printf(TEXT("Image_TabSelected_%d"), I)))))
		{
			SelectedImage->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible
												   : ESlateVisibility::Collapsed);
		}
	}
	ECSJournalRace Race;
	if (GetSelectedRace(Race))
	{
		FString Text;
		FString LifeText;
		FString PreferenceText;
		for (const auto& Entry : GetRaceEntries(Race))
		{
			Text +=
				Entry.Title.ToString() + TEXT("\n\n") +
				(Entry.bUnlocked ? Entry.Body.ToString() : TEXT("이 종족과 관련된 건물을 방문해 상호작용해 주세요.")) +
				TEXT("\n\n\n");
			if (Entry.bUnlocked)
			{
				if (!LifeText.IsEmpty()) LifeText += TEXT("\n\n");
				if (!PreferenceText.IsEmpty()) PreferenceText += TEXT("\n\n");
				LifeText += Entry.LifeBody.ToString();
				PreferenceText += Entry.PreferenceBody.ToString();
			}
		}
		if (Text.IsEmpty())
			Text = TEXT("아직 등록된 정보가 없습니다.");
		if (LifeText.IsEmpty()) LifeText = TEXT("이 종족과 관련된 건물을 방문해 상호작용해 주세요.");
		if (PreferenceText.IsEmpty()) PreferenceText = TEXT("이 종족과 관련된 건물을 방문해 상호작용해 주세요.");
		SetLabel(FName(*FString::Printf(TEXT("RaceBody_%d"), static_cast<int32>(Race))), Text);
		SetLabel(GetRaceDetailLabelName(TEXT("RaceLifeBody"), Race), LifeText);
		SetLabel(GetRaceDetailLabelName(TEXT("RacePreferenceBody"), Race), PreferenceText);
	}
	if (SelectedPage == ECSJournalPage::Calendar)
	{
		SetLabel(TEXT("MonthText"),
				 FString::Printf(TEXT("%d년 %d월"), SelectedMonthIndex / 12 + 1, SelectedMonthIndex % 12 + 1));
		const auto Selected = GetDateInfo(SelectedBusinessDay);
		SetLabel(TEXT("CalendarDetailDateText"),
				 FString::Printf(TEXT("%d년 %d월 %d일"), Selected.Year, Selected.Month, Selected.Day));
		RefreshCalendarEvents();
		for (const auto& Day : GetCalendarDays())
		{
			const FString Suffix = FString::FromInt(Day.DayOfMonth);
			if (auto* DayLabel = Cast<UTextBlock>(GetWidgetFromName(FName(*(TEXT("DayLabel_") + Suffix)))))
			{
				DayLabel->SetText(FText::FromString(
					FString::Printf(TEXT("%d%s\n%s"), Day.DayOfMonth, Day.bToday ? TEXT(" 오늘") : TEXT(""),
						Day.EventCount ? *FString::Printf(TEXT("%s %d"), TEXT("●"), Day.EventCount) : TEXT(""))));
				DayLabel->SetColorAndOpacity(FSlateColor(
					Day.bSelected ? SelectedDayTextColor
						: (Day.bAvailable ? DefaultDayTextColor : UnavailableDayTextColor)));
			}
			if (auto* SelectedImage = Cast<UImage>(GetWidgetFromName(
				FName(*(TEXT("Image_DaySelected_") + Suffix)))))
			{
				SelectedImage->SetVisibility(Day.bSelected ? ESlateVisibility::HitTestInvisible
														: ESlateVisibility::Collapsed);
			}
			if (auto* Button = Cast<UButton>(GetWidgetFromName(FName(*(TEXT("DayButton_") + Suffix)))))
			{
				Button->SetIsEnabled(Day.bAvailable);
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
	FString Result = Order.RecipientDisplayName.ToString() + TEXT("\n") + Status + TEXT("\n");
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
	const TArray<FCSDeliveryOrderRuntime> ActiveOrders = GetAcceptedDeliveryOrders();
	const bool bHasActiveOrder = !ActiveOrders.IsEmpty();
	if (UWidget* Section = GetWidgetFromName(TEXT("ActiveDeliverySection")))
		Section->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UWidget* EmptyText = GetWidgetFromName(TEXT("ActiveDeliveryEmptyText")))
		EmptyText->SetVisibility(ESlateVisibility::Collapsed);
	if (UWidget* DeadlineSection = GetWidgetFromName(TEXT("DeliveryDeadlineSection")))
		DeadlineSection->SetVisibility(bHasActiveOrder ? ESlateVisibility::SelfHitTestInvisible
												  : ESlateVisibility::Collapsed);

	if (bHasActiveOrder)
	{
		const FCSDeliveryOrderRuntime& Order = ActiveOrders[0];
		SetLabel(TEXT("DeliveryDestinationText"), Order.RecipientDisplayName);

		for (int32 I = 0; I < 3; ++I)
		{
			const int32 SlotNumber = I + 1;
			const bool bHasProduct = Order.RequiredProducts.IsValidIndex(I);
			if (UWidget* ProductSection = GetWidgetFromName(
				FName(*FString::Printf(TEXT("DeliveryProductSection_%d"), SlotNumber))))
			{
				ProductSection->SetVisibility(bHasProduct ? ESlateVisibility::SelfHitTestInvisible
														 : ESlateVisibility::Collapsed);
			}
			if (!bHasProduct)
				continue;

			const FCSDeliveryOrderLine& Line = Order.RequiredProducts[I];
			const FCSProductDataRow* Product = Line.ProductRow.GetRow<FCSProductDataRow>(TEXT("JournalDeliveryDetail"));
			SetLabel(FName(*FString::Printf(TEXT("DeliveryProductName_%d"), SlotNumber)),
				Product ? Product->DisplayName : FText::FromName(Line.ProductRow.RowName));
			SetLabel(FName(*FString::Printf(TEXT("DeliveryProductQuantity_%d"), SlotNumber)),
				FString::FromInt(Line.Quantity));
		}

		const int32 DeadlineMinutes = FMath::Max(0, FMath::FloorToInt(Order.DeliveryDeadlineMinutes));
		SetLabel(TEXT("DeliveryDeadlineText"),
			FString::Printf(TEXT("%02d : %02d"), (DeadlineMinutes / 60) % 24, DeadlineMinutes % 60));
	}
	else
	{
		SetLabel(TEXT("DeliveryDestinationText"), TEXT("현재 진행중인 배달이 없습니다"));
		SetLabel(TEXT("DeliveryDeadlineText"), FText::GetEmpty());
		for (int32 SlotNumber = 1; SlotNumber <= 3; ++SlotNumber)
		{
			if (UWidget* ProductSection = GetWidgetFromName(
				FName(*FString::Printf(TEXT("DeliveryProductSection_%d"), SlotNumber))))
				ProductSection->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	FString History;
	for (const auto& Record : GetFinishedDeliveryHistory())
	{
		if (!History.IsEmpty())
			History += TEXT("\n────────────────────────\n\n");
		const auto Start = GetDateInfo(Record.AcceptedDay), End = GetDateInfo(Record.FinishedDay);
		History += FString::Printf(TEXT("%d년 %d월 %d일 수락 → %d년 %d월 %d일 종료\n"), Start.Year, Start.Month,
								   Start.Day, End.Year, End.Month, End.Day) +
				   OrderDescription(Record.Order);
	}
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
	Grid->SetSlotPadding(ArtifactCellPadding);
	const auto Entries = GetArtifactEntries();
	if (DisplayedCatalog != Journal->Catalog || DisplayedArtifactCount != Entries.Num() ||
		ArtifactCells.Num() != Entries.Num())
	{
		Grid->ClearChildren();
		ArtifactCells.Reset();
		ArtifactClickProxies.Reset();
		DisplayedCatalog = Journal->Catalog;
		DisplayedArtifactCount = Entries.Num();
		if (ArtifactCellClass)
			for (int32 I = 0; I < Entries.Num(); ++I)
			{
				auto* Cell = CreateWidget<UUserWidget>(GetGameInstance(), ArtifactCellClass);
				if (!Cell)
					continue;
				if (UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Cell, I / 4, I % 4))
				{
					GridSlot->SetHorizontalAlignment(HAlign_Fill);
					GridSlot->SetVerticalAlignment(VAlign_Fill);
				}
				ArtifactCells.Add(Cell);
			}
	}
	for (int32 I = 0; I < ArtifactCells.Num() && I < Entries.Num(); ++I)
	{
		auto* Cell = ArtifactCells[I].Get();
		const auto& Entry = Entries[I];
		const bool bSelected = Entry.bCollected && Entry.ArtifactId == SelectedArtifactId;
		if (auto* Label = Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ArtifactNameText"))))
		{
			Label->SetText(Entry.Name);
			Label->SetColorAndOpacity(FSlateColor(bSelected ? ArtifactSelectedTextColor : ArtifactDefaultTextColor));
		}
		if (auto* Icon = Cast<UImage>(Cell->GetWidgetFromName(TEXT("ArtifactIcon"))))
		{
			Icon->SetBrushFromTexture(Entry.Icon);
			// 미획득 아이콘도 원본 형상을 보여 주고 별도의 반투명 덮개로 잠금 상태를 표현합니다.
			Icon->SetColorAndOpacity(FLinearColor::White);
		}
		if (UWidget* SelectedImage = Cell->GetWidgetFromName(TEXT("ArtifactSelectedImage")))
			SelectedImage->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible
												 : ESlateVisibility::Collapsed);
		if (UWidget* LockedCover = Cell->GetWidgetFromName(TEXT("ArtifactLockedCover")))
			LockedCover->SetVisibility(Entry.bCollected ? ESlateVisibility::Collapsed
												 : ESlateVisibility::Visible);
		if (auto* CountLabel = Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ArtifactCountText"))))
		{
			const UCSArtifactSubsystem* Artifacts = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>() : nullptr;
			CountLabel->SetText(FText::FromString(FString::Printf(
				TEXT("보유 %d개"), Artifacts ? Artifacts->GetCount(Entry.ArtifactId) : 0)));
			CountLabel->SetColorAndOpacity(FSlateColor(bSelected ? ArtifactSelectedTextColor : ArtifactDefaultTextColor));
			CountLabel->SetVisibility(Entry.bCollected ? ESlateVisibility::HitTestInvisible
												 : ESlateVisibility::Collapsed);
		}
		if (auto* Button = Cast<UButton>(Cell->GetWidgetFromName(TEXT("ArtifactButton"))))
		{
			Button->SetIsEnabled(Entry.bCollected);
			if (auto* ActionButton = Cast<UCSJournalActionButton>(Button))
			{
				ActionButton->Action = ECSJournalButtonAction::Artifact;
				ActionButton->ArtifactId = Entry.ArtifactId;
				ActionButton->Connect(this);
			}
			else
			{
				while (ArtifactClickProxies.Num() <= I)
					ArtifactClickProxies.Add(NewObject<UCSJournalArtifactClickProxy>(this));
				if (UCSJournalArtifactClickProxy* Proxy = ArtifactClickProxies[I])
				{
					Proxy->Connect(this, Entry.ArtifactId);
					Button->OnClicked.AddUniqueDynamic(Proxy, &UCSJournalArtifactClickProxy::Execute);
				}
			}
		}
	}
	FCSJournalArtifactView Selected;
	FString BasicDescription = TEXT("칸을 선택하면 아티팩트 정보를 확인할 수 있습니다.");
	FString AdditionalInfo;
	SetLabel(TEXT("ArtifactDetailNameText"), FString());
	if (auto* DetailIcon = Cast<UImage>(GetWidgetFromName(TEXT("ArtifactDetailIcon"))))
	{
		DetailIcon->SetBrushFromTexture(nullptr);
		DetailIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
	bool bCanUse = false;
	bool bHasUseAction = false;
	FText UseLabel = ArtifactUseConfirmation.IsEmpty()
		? NSLOCTEXT("Journal", "UseArtifact", "아티팩트 사용") : NSLOCTEXT("Artifact", "ConfirmPartialUse", "이대로 사용");
	if (GetSelectedArtifact(Selected))
	{
		SetLabel(TEXT("ArtifactDetailNameText"), Selected.bCollected ? Selected.Name.ToString() : FString(TEXT("???")));
		if (auto* DetailIcon = Cast<UImage>(GetWidgetFromName(TEXT("ArtifactDetailIcon"))))
		{
			DetailIcon->SetBrushFromTexture(Selected.Icon);
			DetailIcon->SetColorAndOpacity(FLinearColor::White);
			DetailIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		BasicDescription = Selected.bCollected
			? Selected.Description.ToString()
			: FString(TEXT("아직 획득하지 못한 아티팩트입니다."));
		if (Selected.bCollected)
		{
			AdditionalInfo = FString::Printf(TEXT("보유: %d개"), GetSelectedArtifactCount());
			if (GetSelectedArtifactStacks() > 0) AdditionalInfo += FString::Printf(TEXT("\n적용 중첩: %d회"), GetSelectedArtifactStacks());
			if (GetSelectedArtifactNextAvailableDay() > GetCurrentBusinessDay())
				AdditionalInfo += FString::Printf(TEXT("\n다음 사용 가능: %d일차"), GetSelectedArtifactNextAvailableDay());
			if (const UCSArtifactSubsystem* Artifacts = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCSArtifactSubsystem>() : nullptr)
			{
				FCSJournalArtifactDataRow Definition;
				const bool bHasDefinition = Artifacts->GetDefinition(SelectedArtifactId, Definition);
				bHasUseAction = bHasDefinition && Definition.Effect != ECSArtifactEffectType::None;
				if (bHasDefinition && Definition.Effect == ECSArtifactEffectType::ChangeWeather)
				{
					const UCSWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UCSWeatherSubsystem>() : nullptr;
					if (Weather && Weather->GetFixedClearBusinessDays() > 0 &&
						Weather->GetCurrentBusinessDay() <= Weather->GetFixedClearBusinessDays())
					{
						AdditionalInfo += FString::Printf(TEXT("\n%d일차부터 사용 가능 (맑은 날씨 유지 기간)"), Weather->GetFixedClearBusinessDays() + 1);
					}
				}
				bCanUse = GetSelectedArtifactCount() > 0 &&
					GetSelectedArtifactNextAvailableDay() <= GetCurrentBusinessDay() &&
					bHasDefinition &&
					Definition.Effect != ECSArtifactEffectType::None;
				if (bHasDefinition && Definition.Effect == ECSArtifactEffectType::FairyContract)
				{
					const bool bSigned = Artifacts->IsFairyContractSigned();
					UseLabel = bSigned ? NSLOCTEXT("Artifact", "FairyContractSignedLabel", "계약 완료")
						: NSLOCTEXT("Artifact", "FairyContractSignLabel", "계약하기");
					AdditionalInfo += bSigned ? TEXT("\n계약 완료 · 미화의 요정이 청소를 도와줍니다.")
						: TEXT("\n사용하면 요정 등장 연출 후 청소를 시작합니다.");
					bCanUse = bCanUse && !bSigned && Artifacts->IsFairyHousePlaced(this);
				}
			}
		}
	}
	if (!ArtifactUseConfirmation.IsEmpty()) AdditionalInfo += TEXT("\n\n") + ArtifactUseConfirmation.ToString();
	SetLabel(TEXT("ArtifactBasicDescriptionText"), BasicDescription);
	SetLabel(TEXT("ArtifactAdditionalInfoText"), AdditionalInfo);
	SetLabel(TEXT("ArtifactDetailUseLabel"), UseLabel);
	if (UWidget* UseButtonOverlay = GetWidgetFromName(TEXT("ArtifactDetailUseButtonOverlay")))
		UseButtonOverlay->SetVisibility(bHasUseAction ? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	if (ArtifactCancelUseButton) ArtifactCancelUseButton->SetVisibility(ArtifactUseConfirmation.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (ArtifactUseButton) ArtifactUseButton->SetIsEnabled(bCanUse);
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
