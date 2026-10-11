#include "UI/Gacha/CSGachaWellWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/CSJournalTypes.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Features/Gacha/CSGachaMachine.h"
#include "InputCoreTypes.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UObject/ConstructorHelpers.h"

UCSGachaWellWidget::UCSGachaWellWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
	SetIsFocusable(true);
	static ConstructorHelpers::FObjectFinder<UCSProductVisualRegistry> V(TEXT("/Game/ConvenienceStore/Data/DataAsset/DA_ProductVisualRegistry"));
	ProductVisuals=V.Object;
}

TSharedRef<SWidget> UCSGachaWellWidget::RebuildWidget()
{
	if (!WidgetTree) Initialize();
	if (!WidgetTree->RootWidget)
	{
		UTextBlock* Hint=nullptr;
		auto* Body=CSFantasyShop::Body(WidgetTree,ShopStyle,NSLOCTEXT("Gacha","WellName","신비한 우물"),
			NSLOCTEXT("Gacha","WellHint","안에 담긴 보물을 살펴보세요"),FVector2D(760,300),&Hint);
		BrowseHint=Hint; BrowseHint->SetVisibility(ESlateVisibility::Collapsed);
		auto MakeButton=[this](FName Name,const FText& Text)
		{
			auto* B=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name); CSFantasyShop::Button(B,ShopStyle);
			B->SetContent(CSFantasyShop::Text(WidgetTree,ShopStyle,Text,18)); return B;
		};
		Menu=WidgetTree->ConstructWidget<UVerticalBox>(); auto* MS=Body->AddChildToVerticalBox(Menu); MS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); MS->SetVerticalAlignment(VAlign_Center);
		DrawButton=MakeButton(TEXT("DrawButton"),FText::GetEmpty()); CostLabel=CastChecked<UTextBlock>(DrawButton->GetContent());
		BrowseButton=MakeButton(TEXT("BrowseButton"),NSLOCTEXT("Gacha","Browse","들여다보기"));
		CSFantasyShop::Button(DrawButton,ShopStyle,true);
		auto* Choices=WidgetTree->ConstructWidget<UHorizontalBox>(); Menu->AddChildToVerticalBox(Choices)->SetHorizontalAlignment(HAlign_Center);
		for (auto* B:{DrawButton.Get(),BrowseButton.Get()}) Choices->AddChildToHorizontalBox(CSFantasyShop::ButtonSize(WidgetTree,B))->SetPadding(FMargin(10,0));
		ListPanel=WidgetTree->ConstructWidget<USizeBox>(); ListPanel->SetHeightOverride(360); Body->AddChildToVerticalBox(ListPanel);
		auto* Frame=CSFantasyShop::ListFrame(WidgetTree,ShopStyle); ListPanel->SetContent(Frame);
		auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Frame->SetContent(Scroll);
		RewardList=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(RewardList);
		auto* Buttons=WidgetTree->ConstructWidget<UHorizontalBox>(); auto* Footer=Body->AddChildToVerticalBox(Buttons); Footer->SetPadding(FMargin(0,18,0,0)); Footer->SetHorizontalAlignment(HAlign_Center);
		BackButton=MakeButton(TEXT("BackButton"),NSLOCTEXT("Gacha","Back","뒤로가기"));
		CloseButton=MakeButton(TEXT("CloseButton"),NSLOCTEXT("Gacha","Close","닫기"));
		for(auto* B:{BackButton.Get(),CloseButton.Get()}) Buttons->AddChildToHorizontalBox(CSFantasyShop::ButtonSize(WidgetTree,B))->SetPadding(FMargin(10,0));
	}
	return Super::RebuildWidget();
}

void UCSGachaWellWidget::NativeConstruct()
{
	Super::NativeConstruct();
	DrawButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Draw);
	BrowseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Browse);
	BackButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Back);
	CloseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Close);
	State=GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (State) State->OnDailySummaryRequested.AddUniqueDynamic(this,&ThisClass::Close);
	if (auto* PC=Cast<ACSStorePlayerController>(GetOwningPlayer())) PC->SetGameplayUIInputLock(this,true);
	const bool HasPool=IsValid(Machine)&&Machine->RewardPool;
	CostLabel->SetText(HasPool ? FText::Format(NSLOCTEXT("Gacha","PutGold","{0}골드 넣기"),FText::AsNumber(Machine->RewardPool->CostGold)) : NSLOCTEXT("Gacha","Unavailable","이용 불가"));
	DrawButton->SetIsEnabled(HasPool); Back();
}

void UCSGachaWellWidget::NativeDestruct()
{
	if (State) State->OnDailySummaryRequested.RemoveDynamic(this,&ThisClass::Close);
	if (auto* PC=Cast<ACSStorePlayerController>(GetOwningPlayer())) PC->SetGameplayUIInputLock(this,false);
	Super::NativeDestruct();
}

void UCSGachaWellWidget::Draw()
{
	// 결제/추첨/연출은 모두 기존 Actor 경로에서만 실행합니다.
	if (IsValid(Machine) && Machine->TryStartDraw(GetOwningPlayerPawn())) Close();
}

void UCSGachaWellWidget::Browse()
{
	if (!IsValid(Machine) || !Machine->RewardPool) { Close(); return; }
	bBrowsing=true; Menu->SetVisibility(ESlateVisibility::Collapsed);
	BrowseHint->SetVisibility(ESlateVisibility::Visible);
	ListPanel->SetVisibility(ESlateVisibility::Visible); BackButton->SetVisibility(ESlateVisibility::Visible);
	BackButton->GetParent()->SetVisibility(ESlateVisibility::Visible); CSFantasyShop::SetPanelSize(WidgetTree,FVector2D(760,660));
	RewardList->ClearChildren(); DisplayedRewardCount=0;
	const auto* Pricing=GetWorld()->GetSubsystem<UCSStorePricingSubsystem>();
	for (const auto& Reward:Machine->GetBrowseRewards())
	{
		UTexture2D* Icon=nullptr; FText Name=Reward.DisplayName;
		FText Value=NSLOCTEXT("Gacha","NoAppraisal","감정가 정보 없음");
		bool bHasAppraisal=false;
		if (Reward.RewardType==ECSGachaRewardType::Product)
		{
			FDataTableRowHandle Handle=Reward.ProductRow;
			if (!Handle.DataTable) Handle.DataTable=Machine->RewardPool->ProductTable;
			if (Handle.RowName.IsNone()) Handle.RowName=Reward.RewardId;
			if (const auto* Data=Handle.GetRow<FCSProductDataRow>(TEXT("Well browse")))
			{
				Name=Data->DisplayName;
				Value=FText::AsNumber(Pricing ? Pricing->GetSalePrice(Handle) : Data->RecommendedSalePrice);
				bHasAppraisal=true;
			}
			const auto* Visuals=Cast<UCSProductVisualRegistry>(Reward.SourceCatalog);
			if (!Visuals) Visuals=ProductVisuals;
			if (Visuals) Icon=Visuals->GetProductIcon(Handle.RowName).LoadSynchronous();
		}
		else if (Reward.RewardType==ECSGachaRewardType::Artifact)
		{
			FDataTableRowHandle Handle=Reward.ArtifactRow;
			if (!Handle.DataTable) Handle.DataTable=Machine->RewardPool->ArtifactTable;
			if (Handle.RowName.IsNone()) Handle.RowName=Reward.RewardId;
			if (const auto* Data=Handle.GetRow<FCSJournalArtifactDataRow>(TEXT("Well browse"))) { Name=Data->DisplayName; Icon=Data->Icon; }
		}
		auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
		auto* IconSize=WidgetTree->ConstructWidget<USizeBox>(); IconSize->SetWidthOverride(64); IconSize->SetHeightOverride(64);
		auto* Image=WidgetTree->ConstructWidget<UImage>(); Image->SetBrushFromTexture(Icon); IconSize->SetContent(Image);
		Row->AddChildToHorizontalBox(IconSize)->SetPadding(FMargin(0,0,16,0));
		auto* N=CSFantasyShop::Text(WidgetTree,ShopStyle,Name.IsEmpty()?FText::FromName(Reward.RewardId):Name,20);
		auto* NS=Row->AddChildToHorizontalBox(N); NS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); NS->SetVerticalAlignment(VAlign_Center);
		auto* ValueSize=WidgetTree->ConstructWidget<USizeBox>(); ValueSize->SetWidthOverride(244); ValueSize->SetHeightOverride(48);
		auto* ValueFrame=WidgetTree->ConstructWidget<UBorder>(); ValueSize->SetContent(ValueFrame);
		ValueFrame->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.0075f,.0075f,.0075f),2.f));
		ValueFrame->SetPadding(FMargin(10,3)); ValueFrame->SetVerticalAlignment(VAlign_Center); ValueFrame->SetHorizontalAlignment(HAlign_Right);
		ValueFrame->SetToolTipText(NSLOCTEXT("Gacha","AppraisalLabel","감정가"));
		auto* ValueRow=WidgetTree->ConstructWidget<UHorizontalBox>(); ValueFrame->SetContent(ValueRow);
		// Keep the label and missing-value message in the same shop typeface.
		auto* Label=CSFantasyShop::Text(WidgetTree,ShopStyle,NSLOCTEXT("Gacha","AppraisalLabel","감정가"),18);
		Label->SetAutoWrapText(false); Label->SetColorAndOpacity(FSlateColor(FLinearColor(1,.752942f,.250158f)));
		Label->SetVisibility(bHasAppraisal?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
		// Compensate for the different visible glyph heights in Title and Info.
		auto* LabelSlot=ValueRow->AddChildToHorizontalBox(Label); LabelSlot->SetVerticalAlignment(VAlign_Center); LabelSlot->SetPadding(FMargin(0,6,6,0));
		// The existing currency glyph is available in Info, but not in the Title typeface.
		auto* Diamond=CSFantasyShop::Text(WidgetTree,ShopStyle,NSLOCTEXT("Gacha","AppraisalDiamond","◆"),18);
		auto DiamondFont=Diamond->GetFont(); DiamondFont.TypefaceFontName=TEXT("Info"); Diamond->SetFont(DiamondFont);
		Diamond->SetAutoWrapText(false);
		Diamond->SetColorAndOpacity(FSlateColor(FLinearColor(1,.752942f,.250158f)));
		Diamond->SetVisibility(bHasAppraisal?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
		auto* DS=ValueRow->AddChildToHorizontalBox(Diamond); DS->SetVerticalAlignment(VAlign_Center); DS->SetPadding(FMargin(0,10,6,0));
		auto* V=CSFantasyShop::Text(WidgetTree,ShopStyle,Value,bHasAppraisal?24:18);
		V->SetColorAndOpacity(FSlateColor(FLinearColor(1,.752942f,.250158f)));
		V->SetJustification(ETextJustify::Right); V->SetAutoWrapText(false);
		// Reserve five digits plus a separator so labels and currency symbols stay in fixed columns.
		auto* AmountSize=WidgetTree->ConstructWidget<USizeBox>(); AmountSize->SetContent(V);
		if (bHasAppraisal) { AmountSize->SetWidthOverride(100); AmountSize->SetRenderTranslation(FVector2D(0,4)); }
		auto* AmountSlot=ValueRow->AddChildToHorizontalBox(AmountSize); AmountSlot->SetVerticalAlignment(VAlign_Center);
		auto* VS=Row->AddChildToHorizontalBox(ValueSize); VS->SetVerticalAlignment(VAlign_Center); VS->SetPadding(FMargin(12,0,0,0));
		RewardList->AddChildToVerticalBox(Row)->SetPadding(FMargin(4,8)); ++DisplayedRewardCount;
	}
	if (!DisplayedRewardCount) RewardList->AddChildToVerticalBox(CSFantasyShop::Text(WidgetTree,ShopStyle,NSLOCTEXT("Gacha","NoAvailableRewards","현재 획득 가능한 보상이 없습니다.")));
}

void UCSGachaWellWidget::Back()
{
	bBrowsing=false; Menu->SetVisibility(ESlateVisibility::Visible);
	BrowseHint->SetVisibility(ESlateVisibility::Collapsed);
	ListPanel->SetVisibility(ESlateVisibility::Collapsed); BackButton->SetVisibility(ESlateVisibility::Collapsed);
	BackButton->GetParent()->SetVisibility(ESlateVisibility::Collapsed); CSFantasyShop::SetPanelSize(WidgetTree,FVector2D(760,300));
}

void UCSGachaWellWidget::Close()
{
	if (auto* PC=Cast<ACSStorePlayerController>(GetOwningPlayer())) PC->SetGameplayUIInputLock(this,false);
	RemoveFromParent();
}

FReply UCSGachaWellWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
	if (Event.GetKey()==EKeys::Escape) { Close(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry,Event);
}
