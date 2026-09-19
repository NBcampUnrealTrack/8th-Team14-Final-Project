#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSTutorialTypes.h"
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

	// 대화창 힌트 줄(키 아이콘 + 보조 설명)이 바뀔 때마다 호출됨
	// HintKey가 비어있으면 키 아이콘을, HintText가 비어있으면 힌트 줄 전체를 숨겨야 함
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnDialogueHintChanged(const FText& HintKey, const FText& HintText);

	// 키가 여러 개라 한 줄 힌트로 못 담는 스텝(이동, 건축 회전 등)의 여러 줄 힌트가 바뀔 때마다 호출됨
	// 빈 배열이면 이 목록 UI를 숨겨야 함. OnDialogueHintChanged와 별개로 항상 같이 호출됨
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnDialogueHintListChanged(const TArray<FCSTutorialHintEntry>& Hints);

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
