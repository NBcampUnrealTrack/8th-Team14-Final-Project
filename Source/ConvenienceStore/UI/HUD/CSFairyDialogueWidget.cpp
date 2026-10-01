#include "UI/HUD/CSFairyDialogueWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/BreakIterator.h"

TSharedRef<SWidget> UCSFairyDialogueWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		// 별도의 WBP를 지정하지 않아도 말풍선과 연출 건너뛰기를 사용하실 수 있습니다.
		UBorder* Border = WidgetTree->ConstructWidget<UBorder>();
		Border->SetBrushColor(FLinearColor(.04f, .025f, .08f, .94f));
		Border->SetPadding(FMargin(20.f, 12.f));
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		Border->SetContent(Box);
		DialogueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogueText"));
		DialogueText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		DialogueText->SetFont(FSlateFontInfo(DialogueText->GetFont().FontObject, 20));
		DialogueText->SetWrapTextAt(480.f);
		Box->AddChildToVerticalBox(DialogueText);
		UTextBlock* Tail = WidgetTree->ConstructWidget<UTextBlock>();
		Tail->SetText(FText::FromString(TEXT("▼")));
		Tail->SetColorAndOpacity(FSlateColor(FLinearColor(.8f, .65f, 1.f)));
		Box->AddChildToVerticalBox(Tail)->SetHorizontalAlignment(HAlign_Center);
		AdvanceHint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AdvanceHint"));
		AdvanceHint->SetFont(FSlateFontInfo(DialogueText->GetFont().FontObject, 14));
		AdvanceHint->SetColorAndOpacity(FSlateColor(FLinearColor(.8f, .7f, 1.f)));
		Box->AddChildToVerticalBox(AdvanceHint)->SetPadding(FMargin(0.f, 8.f));
		SkipButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SkipButton"));
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(NSLOCTEXT("Fairy", "SkipIntroduction", "전체 건너뛰기 (Esc)"));
		SkipButton->SetContent(Label);
		Box->AddChildToVerticalBox(SkipButton)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		WidgetTree->RootWidget = Border;
	}
	SetIsFocusable(true);
	RefreshText();
	return Super::RebuildWidget();
}

void UCSFairyDialogueWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// WBP에서 만든 버튼에도 기존 건너뛰기 동작을 연결해 드립니다.
	if (SkipButton) SkipButton->OnClicked.AddUniqueDynamic(this, &UCSFairyDialogueWidget::Skip);
	RefreshText();
}

void UCSFairyDialogueWidget::SetDialogue(const FText& Text, bool bAllowSkip)
{
	PendingText = Text;
	bSkippable = bAllowSkip;
	bTyping = false;
	bCanAdvance = false;
	RefreshText();
}

void UCSFairyDialogueWidget::StartTypewriterDialogue(const FText& Text, float CharactersPerSecond)
{
	PendingText = Text;
	FullText = Text.ToString();
	TypewriterSpeed = FMath::Max(1.f, CharactersPerSecond);
	TypewriterStart = FPlatformTime::Seconds();
	VisibleCharacters = 0;
	CharacterBoundaries.Reset();
	// 한글과 이모지의 글자 경계를 사용하여 표시 중 글자가 깨지지 않도록 합니다.
	const TSharedRef<IBreakIterator> Iterator = FBreakIterator::CreateCharacterBoundaryIterator();
	Iterator->SetStringRef(FullText);
	Iterator->ResetToBeginning();
	for (int32 Boundary = Iterator->MoveToNext(); Boundary != INDEX_NONE; Boundary = Iterator->MoveToNext())
		CharacterBoundaries.Add(Boundary);
	bTyping = !CharacterBoundaries.IsEmpty();
	bSkippable = true;
	bCanAdvance = true;
	RefreshText();
}

void UCSFairyDialogueWidget::RefreshText()
{
	if (DialogueText)
	{
		const int32 Length = VisibleCharacters > 0 ? CharacterBoundaries[VisibleCharacters - 1] : 0;
		DialogueText->SetText(bTyping ? FText::FromString(FullText.Left(Length)) : PendingText);
	}
	if (SkipButton) SkipButton->SetVisibility(bSkippable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (AdvanceHint)
	{
		AdvanceHint->SetVisibility(bCanAdvance ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		AdvanceHint->SetText(bTyping ? NSLOCTEXT("Fairy", "RevealLine", "Space: 전체 대사 보기")
			: NSLOCTEXT("Fairy", "NextLine", "Space: 다음 대사"));
	}
}

void UCSFairyDialogueWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	if (!bTyping) return;
	// 영업이 일시정지되어도 대사는 실제 경과 시간에 맞춰 표시합니다.
	const int32 Count = FMath::Clamp(FMath::FloorToInt((FPlatformTime::Seconds() - TypewriterStart) * TypewriterSpeed), 0, CharacterBoundaries.Num());
	if (Count == VisibleCharacters) return;
	VisibleCharacters = Count;
	bTyping = Count < CharacterBoundaries.Num();
	RefreshText();
}

void UCSFairyDialogueWidget::Advance()
{
	if (!bCanAdvance) return;
	if (bTyping)
	{
		bTyping = false;
		VisibleCharacters = CharacterBoundaries.Num();
		RefreshText();
	}
	else OnAdvance.Broadcast();
}

FReply UCSFairyDialogueWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (bSkippable && Event.GetKey() == EKeys::Escape)
	{
		if (!Event.IsRepeat()) Skip();
		return FReply::Handled();
	}
	if (bSkippable && Event.GetKey() == EKeys::SpaceBar)
	{
		// 키를 길게 눌러도 여러 문장이 한꺼번에 넘어가지 않도록 합니다.
		if (!Event.IsRepeat()) Advance();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UCSFairyDialogueWidget::Skip()
{
	if (bSkippable) OnSkip.Broadcast();
}
