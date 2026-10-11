#include "UI/Audio/CSFantasyShopStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

FCSFantasyShopStyle::FCSFantasyShopStyle()
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> P(TEXT("/Game/ConvenienceStore/UI/Employee/Texture/T_employeebg"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> B(TEXT("/Game/ConvenienceStore/UI/NewTitle/Texture/T_START_NBUTTON"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> S(TEXT("/Game/ConvenienceStore/UI/NewTitle/Texture/T_START_BUTTON"));
	static ConstructorHelpers::FObjectFinder<UFont> F(TEXT("/Game/ConvenienceStore/UI/Font/BasicFontFamilly"));
	Panel=P.Object; Button=B.Object; SelectedButton=S.Object; Font=F.Object;
}

UTextBlock* CSFantasyShop::Text(UWidgetTree* Tree, const FCSFantasyShopStyle& Style, const FText& Value, int32 Size)
{
	auto* T=Tree->ConstructWidget<UTextBlock>();
	T->SetText(Value); T->SetFont(FSlateFontInfo(Style.Font,Size,TEXT("Title")));
	T->SetColorAndOpacity(FSlateColor(FLinearColor::White)); T->SetAutoWrapText(true);
	return T;
}

void CSFantasyShop::Button(UButton* B, const FCSFantasyShopStyle& Style, bool bSelected)
{
	auto Brush=[](UTexture2D* Texture)
	{
		FSlateBrush R; R.SetResourceObject(Texture); R.ImageSize=FVector2D(240,64);
		R.DrawAs=ESlateBrushDrawType::Image;
		return R;
	};
	FButtonStyle S; S.SetNormal(Brush(bSelected ? Style.SelectedButton : Style.Button));
	auto Hovered=Brush(Style.SelectedButton); Hovered.TintColor=FSlateColor(FLinearColor(.74f,.74f,.74f)); S.SetHovered(Hovered);
	auto Pressed=Hovered; Pressed.TintColor=FSlateColor(FLinearColor(.63f,.63f,.63f)); S.SetPressed(Pressed);
	auto Disabled=Brush(Style.SelectedButton); Disabled.TintColor=FSlateColor(FLinearColor(.158f,.158f,.158f)); S.SetDisabled(Disabled);
	S.NormalPadding=FMargin(12,2); S.PressedPadding=FMargin(12,3,12,1);
	B->SetStyle(S); B->SetBackgroundColor(FLinearColor::White);
}

USizeBox* CSFantasyShop::ButtonSize(UWidgetTree* Tree, UButton* Button, float Width)
{
	auto* Size=Tree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(Width); Size->SetHeightOverride(56); Size->SetContent(Button); return Size;
}

UBorder* CSFantasyShop::ListFrame(UWidgetTree* Tree, const FCSFantasyShopStyle& Style, bool bOuterFrame)
{
	auto* Frame=Tree->ConstructWidget<UBorder>();
	if (bOuterFrame)
	{
		// Reuse the hiring panel's outer frame without its baked-in wage rows and icons.
		FSlateBrush Brush; Brush.SetResourceObject(Style.Panel);
		Brush.DrawAs=ESlateBrushDrawType::Border; Brush.Margin=FMargin(.065f,.105f);
		Frame->SetBrush(Brush);
	}
	else
	{
		Frame->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.0075f,.0075f,.0075f),4.f,FLinearColor(.6f,.42f,.12f),1.f));
	}
	Frame->SetPadding(FMargin(16)); return Frame;
}

void CSFantasyShop::SetPanelSize(UWidgetTree* Tree, FVector2D Size)
{
	if (auto* Panel=Cast<USizeBox>(Tree->FindWidget(TEXT("ShopPanelSize")))) { Panel->SetWidthOverride(Size.X); Panel->SetHeightOverride(Size.Y); }
}

UVerticalBox* CSFantasyShop::Body(UWidgetTree* Tree, const FCSFantasyShopStyle& Style, const FText& Title, const FText& Hint, FVector2D PanelSize, UTextBlock** OutHint)
{
	auto* Canvas=Tree->ConstructWidget<UCanvasPanel>(); Tree->RootWidget=Canvas;
	auto* Shade=Tree->ConstructWidget<UBorder>(); Shade->SetBrushColor(FLinearColor(0,0,0,.65f));
	auto* SS=Canvas->AddChildToCanvas(Shade); SS->SetAnchors(FAnchors(0,0,1,1)); SS->SetOffsets(FMargin(0));
	auto* Scale=Tree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit); Scale->SetStretchDirection(EStretchDirection::DownOnly);
	auto* CS=Canvas->AddChildToCanvas(Scale); CS->SetAnchors(FAnchors(.08f,.06f,.92f,.94f)); CS->SetOffsets(FMargin(0));
	auto* Size=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopPanelSize")); Size->SetWidthOverride(PanelSize.X); Size->SetHeightOverride(PanelSize.Y); Scale->SetContent(Size);
	auto* Layers=Tree->ConstructWidget<UOverlay>(); Size->SetContent(Layers);
	auto* Background=Tree->ConstructWidget<UBorder>(); Background->SetBrushColor(FLinearColor(.0075f,.0075f,.0075f));
	auto* BackgroundSlot=Layers->AddChildToOverlay(Background); BackgroundSlot->SetHorizontalAlignment(HAlign_Fill); BackgroundSlot->SetVerticalAlignment(VAlign_Fill); BackgroundSlot->SetPadding(FMargin(24));
	auto* Panel=ListFrame(Tree,Style,true); auto* PanelSlot=Layers->AddChildToOverlay(Panel); PanelSlot->SetHorizontalAlignment(HAlign_Fill); PanelSlot->SetVerticalAlignment(VAlign_Fill);
	Panel->SetPadding(FMargin(40,32,40,32));
	auto* Body=Tree->ConstructWidget<UVerticalBox>(); Panel->SetContent(Body);
	auto* TitleText=Text(Tree,Style,Title,34); TitleText->SetJustification(ETextJustify::Center); TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(1,.752942f,.250158f)));
	Body->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0,0,0,8));
	auto* HintText=Text(Tree,Style,Hint,20); HintText->SetJustification(ETextJustify::Center); HintText->SetFont(FSlateFontInfo(Style.Font,20,TEXT("SubTitle")));
	if (OutHint) *OutHint=HintText;
	Body->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0,0,0,16));
	auto* DividerSize=Tree->ConstructWidget<USizeBox>(); DividerSize->SetHeightOverride(1);
	auto* Divider=Tree->ConstructWidget<UBorder>(); Divider->SetBrushColor(FLinearColor(.6f,.42f,.12f)); DividerSize->SetContent(Divider);
	Body->AddChildToVerticalBox(DividerSize)->SetPadding(FMargin(48,0,48,18));
	return Body;
}
