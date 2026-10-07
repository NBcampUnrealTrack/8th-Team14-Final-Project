#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTutorialChoiceWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnTutorialChoiceMade, bool, bYes);

// 튜토리얼 중간에 예/아니요를 고르는 창. 문구는 스텝 행에서 받는다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTutorialChoiceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FCSOnTutorialChoiceMade OnChoiceMade;

	void SetTexts(const FText& Question, const FText& YesLabel, const FText& NoLabel);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> QuestionText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> YesButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> NoButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> YesText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> NoText;

private:
	UFUNCTION()
	void HandleYesClicked();

	UFUNCTION()
	void HandleNoClicked();
};
