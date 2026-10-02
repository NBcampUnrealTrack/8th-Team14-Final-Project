#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSFairyDialogueWidget.generated.h"

class UTextBlock;
class UButton;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSFairyDialogueSkip);

UCLASS(Blueprintable)
class CONVENIENCESTORE_API UCSFairyDialogueWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetDialogue(const FText& Text, bool bAllowSkip = false);
	void StartTypewriterDialogue(const FText& Text, float CharactersPerSecond);
	UPROPERTY(BlueprintAssignable) FCSFairyDialogueSkip OnSkip;
	UPROPERTY(BlueprintAssignable) FCSFairyDialogueSkip OnAdvance;
	// 별도의 다음 버튼을 디자인하실 때 이 함수를 연결해 주세요.
	UFUNCTION(BlueprintCallable, Category="Fairy|Dialogue") void Advance();
	UFUNCTION(BlueprintCallable, Category="Fairy|Dialogue") void Skip();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	// 디자이너에서 같은 이름의 TextBlock을 배치해 주세요. 대사 표시와 타이핑은 자동으로 처리합니다.
	UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="Fairy|Dialogue") TObjectPtr<UTextBlock> DialogueText;
	// 일반 말풍선에서는 안내와 건너뛰기 버튼을 생략하셔도 됩니다.
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Fairy|Dialogue") TObjectPtr<UButton> SkipButton;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Fairy|Dialogue") TObjectPtr<UTextBlock> AdvanceHint;
private:
	FText PendingText;
	FString FullText;
	TArray<int32> CharacterBoundaries;
	double TypewriterStart = 0.;
	float TypewriterSpeed = 30.f;
	int32 VisibleCharacters = 0;
	bool bTyping = false;
	bool bCanAdvance = false;
	bool bSkippable = false;
	void RefreshText();
};
