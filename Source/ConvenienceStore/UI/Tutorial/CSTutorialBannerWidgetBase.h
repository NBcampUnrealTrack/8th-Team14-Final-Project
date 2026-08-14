#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTutorialBannerWidgetBase.generated.h"

struct FCSTutorialStepRow;
class UCSTutorialSubsystem;

// 화면 중앙에 잠깐 떴다 사라지는 튜토리얼 배너의 기반 클래스
// 비주얼 트리는 없고 서브시스템 방송을 받아 BlueprintImplementableEvent로 전달하는 역할만
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTutorialBannerWidgetBase : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 배너 문구가 바뀔 때마다 호출됨
	// 디자이너는 여기서 텍스트를 반영하고 등장 애니메이션을 재생
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnBannerTextChanged(const FText& NewText);

	// BannerDisplaySeconds가 지나거나 튜토리얼이 끝나면 호출됨
	// 디자이너는 여기서 사라지는 애니메이션을 재생
	UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial")
	void OnBannerHidden();

	// 배너가 화면에 떠 있는 시간(초)
	// WBP의 Class Defaults에서 조정할 수 있음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tutorial")
	float BannerDisplaySeconds = 2.5f;

private:
	UFUNCTION()
	void HandleStepChanged(const FCSTutorialStepRow& NewStep);

	UFUNCTION()
	void HandleTutorialCompleted();

	void HideBanner();

	TObjectPtr<UCSTutorialSubsystem> TutorialSubsystem;
	FTimerHandle BannerHideTimerHandle;
};
