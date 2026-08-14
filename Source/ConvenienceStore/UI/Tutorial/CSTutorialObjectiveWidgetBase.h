#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTutorialObjectiveWidgetBase.generated.h"

struct FCSTutorialStepRow;
class UCSTutorialSubsystem;

// 화면 우측 상단에 상주하는 튜토리얼 목표 표시의 기반 클래스
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTutorialObjectiveWidgetBase : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 목표 문구가 바뀔 때마다 호출됨
	// bShowProgress가 false면 숫자(0/3 등)를 숨기고 문구만 표시 (예: "매장 입구로 이동하세요")
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnObjectiveChanged(const FText& NewText, bool bShowProgress);

	// 진행 수치가 바뀔 때마다 호출 (예: 1/3)
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnObjectiveProgressChanged(int32 Current, int32 Goal);

	// 튜토리얼이 끝났을 때 호출됨
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnObjectiveHidden();

private:
	UFUNCTION()
	void HandleStepChanged(const FCSTutorialStepRow& NewStep);

	UFUNCTION()
	void HandleObjectiveProgress(int32 Current, int32 Goal);

	UFUNCTION()
	void HandleTutorialCompleted();

	TObjectPtr<UCSTutorialSubsystem> TutorialSubsystem;
};
