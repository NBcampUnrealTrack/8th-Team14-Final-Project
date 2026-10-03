#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCraftTypes.h"
#include "CSCraftCloseupWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UCSCraftKeyHintRowWidget;

// 솥 클로즈업 중 하단 키 안내와 알림 한 칸. 입력은 받지 않는다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftCloseupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStep(ECSCraftStep Step);
	// 한 칸이라 새 메시지가 이전 것을 덮고 표시 시간도 다시 센다
	void ShowMessage(const FText& Message, bool bWarning);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> KeyHintList;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftKeyHintRowWidget> KeyHintRowClass;

	// 색은 디자인 시안이 덮어쓸 임시값
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	FSlateColor WarningMessageColor = FSlateColor(FLinearColor(1.0f, 0.35f, 0.15f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	FSlateColor InfoMessageColor = FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f));

	// 공용 배너(WBP_NoticeBanner) 표시 시간과 맞춘다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI", meta = (ClampMin = "0.1"))
	float MessageDisplaySeconds = 2.5f;

private:
	void HideMessage();

	FTimerHandle MessageHideTimerHandle;
};
