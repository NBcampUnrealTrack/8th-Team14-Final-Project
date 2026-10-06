#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSCraftIngredientRowWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;

DECLARE_DELEGATE_OneParam(FCSCraftIngredientRemoveClicked, FName);

// 재료 창 한 줄. 목록 줄 위젯에는 꺼내기 버튼이 없어서 따로 둔다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftIngredientRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetEntry(
		FName InProductId,
		const FText& Name,
		int32 Count,
		UTexture2D* Icon,
		const FText& RemoveLabel,
		FCSCraftIngredientRemoveClicked InOnRemove);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RemoveButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RemoveButtonText;

private:
	UFUNCTION()
	void HandleRemoveClicked();

	FName ProductId;
	FCSCraftIngredientRemoveClicked OnRemove;
};
