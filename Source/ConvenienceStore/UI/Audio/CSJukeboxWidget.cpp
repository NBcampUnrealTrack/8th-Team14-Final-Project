#include "UI/Audio/CSJukeboxWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

namespace
{
// Only track rows use this rectangular version of the existing shop frame palette.
void StyleTrackRow(UButton* Button, bool bSelected)
{
	const FLinearColor Gold(.6f,.42f,.12f), Green(.38f,.72f,.17f);
	FButtonStyle Style;
	Style.SetNormal(FSlateRoundedBoxBrush(bSelected ? FLinearColor(.022f,.045f,.017f) : FLinearColor(.015f,.015f,.015f),0.f,bSelected ? Green : Gold,1.5f));
	Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.035f,.065f,.025f),0.f,Green,2.f));
	Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.012f,.028f,.008f),0.f,Green,2.f));
	Style.SetNormalPadding(FMargin(20,6)); Style.SetPressedPadding(FMargin(20,7,20,5));
	Button->SetStyle(Style);
}
}

void UCSJukeboxTrackButton::Choose() { if (OwnerWidget) OwnerWidget->SelectTrack(TrackId); }

UCSJukeboxWidget::UCSJukeboxWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetIsFocusable(true);
}

TSharedRef<SWidget> UCSJukeboxWidget::RebuildWidget()
{
	if (!WidgetTree) Initialize();
	if (!WidgetTree->RootWidget)
	{
		auto Text = [this](const FText& Value, int32 Size) { return CSFantasyShop::Text(WidgetTree,ShopStyle,Value,Size); };
		auto* Body = CSFantasyShop::Body(WidgetTree,ShopStyle,NSLOCTEXT("Jukebox", "Title", "배경음악"),NSLOCTEXT("Jukebox", "Hint", "음악을 선택하고 적용을 눌러주세요."),FVector2D(760,480));
		auto* ListSize = WidgetTree->ConstructWidget<USizeBox>(); ListSize->SetHeightOverride(200); Body->AddChildToVerticalBox(ListSize);
		auto* Frame=CSFantasyShop::ListFrame(WidgetTree,ShopStyle); ListSize->SetContent(Frame);
		auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Frame->SetContent(Scroll);
		TrackList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TrackList")); Scroll->AddChild(TrackList);
		auto* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(); auto* Footer=Body->AddChildToVerticalBox(Buttons); Footer->SetPadding(FMargin(0,18,0,0)); Footer->SetHorizontalAlignment(HAlign_Center);
		ApplyButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ApplyButton"));
		CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
		ApplyButton->SetContent(Text(NSLOCTEXT("Jukebox", "Apply", "적용"), 18));
		CloseButton->SetContent(Text(NSLOCTEXT("Jukebox", "Close", "닫기"), 18));
		for (auto* Button : {ApplyButton.Get(), CloseButton.Get()})
		{
			CSFantasyShop::Button(Button,ShopStyle,Button==ApplyButton);
			Buttons->AddChildToHorizontalBox(CSFantasyShop::ButtonSize(WidgetTree,Button))->SetPadding(FMargin(10,0));
		}
	}
	return Super::RebuildWidget();
}

void UCSJukeboxWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Audio = UCSAudioSubsystem::Get(this);
	State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	CandidateId = Audio ? Audio->GetSelectedBGMId() : NAME_None;
	if (Audio) Audio->OnBGMChanged.AddUniqueDynamic(this, &ThisClass::RefreshView);
	if (State) State->OnDailySummaryRequested.AddUniqueDynamic(this, &ThisClass::Close);
	ApplyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ApplySelection);
	CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Close);
	Rows.Reset(); TrackList->ClearChildren();
	if (Audio) for (const auto& Track : Audio->GetBGMTracks())
	{
		auto* Row = WidgetTree->ConstructWidget<UCSJukeboxTrackButton>();
		StyleTrackRow(Row,false);
		Row->TrackId = Track.Id; Row->OwnerWidget = this;
		Row->Label = CSFantasyShop::Text(WidgetTree,ShopStyle,FText::GetEmpty(),20);
		Row->StatusLabel = CSFantasyShop::Text(WidgetTree,ShopStyle,FText::GetEmpty(),18);
		Row->Label->SetAutoWrapText(false); Row->StatusLabel->SetAutoWrapText(false);
		auto* Content=WidgetTree->ConstructWidget<UHorizontalBox>();
		auto* NameSlot=Content->AddChildToHorizontalBox(Row->Label); NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NameSlot->SetVerticalAlignment(VAlign_Center);
		auto* StatusSlot=Content->AddChildToHorizontalBox(Row->StatusLabel); StatusSlot->SetPadding(FMargin(20,0,0,0)); StatusSlot->SetVerticalAlignment(VAlign_Center);
		Row->StatusLabel->SetJustification(ETextJustify::Right);
		Row->SetContent(Content); CastChecked<UButtonSlot>(Content->Slot)->SetHorizontalAlignment(HAlign_Fill);
		Row->OnClicked.AddUniqueDynamic(Row, &UCSJukeboxTrackButton::Choose);
		auto* RowSize=WidgetTree->ConstructWidget<USizeBox>(); RowSize->SetHeightOverride(56); RowSize->SetContent(Row);
		auto* RowSlot = TrackList->AddChildToVerticalBox(RowSize); RowSlot->SetPadding(FMargin(0, 5));
		Rows.Add(Row);
	}
	if (auto* PC = Cast<ACSStorePlayerController>(GetOwningPlayer())) PC->SetGameplayUIInputLock(this, true);
	RefreshView();
}

void UCSJukeboxWidget::NativeDestruct()
{
	if (Audio) Audio->OnBGMChanged.RemoveDynamic(this, &ThisClass::RefreshView);
	if (State) State->OnDailySummaryRequested.RemoveDynamic(this, &ThisClass::Close);
	if (auto* PC = Cast<ACSStorePlayerController>(GetOwningPlayer())) PC->SetGameplayUIInputLock(this, false);
	Super::NativeDestruct();
}

void UCSJukeboxWidget::SelectTrack(FName Id)
{
	if (Audio && Audio->GetBGMTracks().ContainsByPredicate([&](const auto& Track) { return Track.Id == Id; })) CandidateId = Id;
	RefreshView();
}

bool UCSJukeboxWidget::CanApply() const
{
	return Audio && CandidateId != Audio->GetSelectedBGMId() &&
		Audio->GetBGMTracks().ContainsByPredicate([&](const auto& Track) { return Track.Id == CandidateId; });
}

void UCSJukeboxWidget::ApplySelection()
{
	if (CanApply()) Audio->SelectBGM(CandidateId);
	RefreshView();
}

void UCSJukeboxWidget::RefreshView()
{
	if (ApplyButton) ApplyButton->SetIsEnabled(CanApply());
	if (!Audio) return;
	for (UCSJukeboxTrackButton* Row : Rows) for (const auto& Track : Audio->GetBGMTracks()) if (Row->TrackId == Track.Id)
	{
		const bool Current = Track.Id == Audio->GetSelectedBGMId();
		const bool Playing = Track.Id == Audio->GetPlayingBGMId();
		const FText Status = Playing ? NSLOCTEXT("Jukebox", "Playing", "현재 재생 중") :
			Current ? NSLOCTEXT("Jukebox", "AppliedCompact", "재생 대기") :
			Track.Id == CandidateId ? NSLOCTEXT("Jukebox", "CandidateCompact", "선택됨") : FText::GetEmpty();
		Row->Label->SetText(Track.DisplayName.IsEmpty() ? FText::FromName(Track.Id) : Track.DisplayName);
		Row->StatusLabel->SetText(Status);
		StyleTrackRow(Row,Track.Id == CandidateId);
	}
}

void UCSJukeboxWidget::Close()
{
	// Slate 해제가 지연되더라도 닫기를 누른 프레임에 기존 입력 규칙으로 복귀합니다.
	if (auto* PC = Cast<ACSStorePlayerController>(GetOwningPlayer())) PC->SetGameplayUIInputLock(this, false);
	RemoveFromParent();
}

FReply UCSJukeboxWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape) { Close(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry, Event);
}
