#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSNoticeBoardRumorTabWidget.generated.h"

class UCSCraftRumorSubsystem;
class UTextBlock;

// 게시판의 "Trend" 탭 자리에 들어가는 소문 탭. 화면 이름은 "소문"이고 게시판 C++의 탭 ID는 그대로 둔다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSNoticeBoardRumorTabWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorText1;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorText2;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorText3;

private:
	UFUNCTION()
	void RefreshRumors();

	TWeakObjectPtr<UCSCraftRumorSubsystem> RumorSubsystem;
};
