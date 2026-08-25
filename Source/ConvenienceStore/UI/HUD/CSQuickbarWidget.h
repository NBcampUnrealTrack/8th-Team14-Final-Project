#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSQuickbarWidget.generated.h"

class UTexture2D;
class UHorizontalBox;
class UCSQuickbarSlotWidget;

// WBP에서 슬롯 아이콘과 선택 테두리를 동적으로 그리기 위한 퀵바 위젯 기반 클래스입니다.
UCLASS()
class CONVENIENCESTORE_API UCSQuickbarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 슬롯 수와 같은 수의 슬롯 위젯을 생성하고, 선택·아이콘 상태를 갱신합니다.
	UFUNCTION(BlueprintCallable, Category = "Quickbar")
	void SetQuickbarData(
		int32 SlotCount,
		int32 SelectedSlotIndex,
		const TArray<FText>& ToolNames,
		const TArray<UTexture2D*>& ToolIcons);

protected:
	// WBP_CSQuickbar에 배치할 슬롯들의 부모 HorizontalBox입니다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> SlotContainer;

	// 각 칸에 사용할 WBP_CSQuickbarSlot 클래스입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quickbar")
	TSubclassOf<UCSQuickbarSlotWidget> QuickbarSlotWidgetClass;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCSQuickbarSlotWidget>> CreatedSlotWidgets;
};
