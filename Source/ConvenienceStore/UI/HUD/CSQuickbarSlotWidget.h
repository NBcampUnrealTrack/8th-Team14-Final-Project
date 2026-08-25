#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSQuickbarSlotWidget.generated.h"

class UBorder;
class UImage;
class UTextBlock;
class UTexture2D;

// 퀵바의 한 칸에 단축키, 도구 아이콘, 선택 상태를 표시합니다.
UCLASS()
class CONVENIENCESTORE_API UCSQuickbarSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSlotData(
		int32 SlotIndex,
		const FText& ToolName,
		UTexture2D* ToolIcon,
		bool bIsSelected);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SelectedBorder;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ToolIconImage;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotKeyText;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ToolNameText;
};
