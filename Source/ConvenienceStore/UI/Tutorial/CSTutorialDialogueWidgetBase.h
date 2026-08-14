#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTutorialDialogueWidgetBase.generated.h"

struct FCSTutorialStepRow;
class UCSTutorialSubsystem;

// 화면 하단에 상주하는 튜토리얼 대화창의 기반 클래스
// PromptType이 Dialogue인 스텝에서만 문구를 표시하고 그 외 스텝(조준 기반)에서는 숨김
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTutorialDialogueWidgetBase : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 대화창 문구가 바뀔 때마다 호출됨
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnDialogueTextChanged(const FText& NewText);

	// 이 스텝이 대화창을 쓰지 않을 때(PromptType != Dialogue) 또는 튜토리얼이 끝났을 때 호출됨
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnDialogueHidden();

private:
	UFUNCTION()
	void HandleStepChanged(const FCSTutorialStepRow& NewStep);

	UFUNCTION()
	void HandleTutorialCompleted();

	TObjectPtr<UCSTutorialSubsystem> TutorialSubsystem;
};
