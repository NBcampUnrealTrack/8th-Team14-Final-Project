#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSCraftListRowWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;

DECLARE_DELEGATE_OneParam(FCSCraftListRowSelected, FName);

// 소환 목록과 자동 제조 창이 같이 쓰는 한 줄. 어느 창에 붙는지는 선택 콜백으로만 안다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftListRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Detail이 비어 있으면 그 칸을 접는다
	void SetEntry(
		FName InKey,
		const FText& Name,
		const FText& Detail,
		int32 Count,
		UTexture2D* Icon,
		bool bSelectable,
		FCSCraftListRowSelected InOnSelected);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SelectButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;

private:
	UFUNCTION()
	void HandleSelectClicked();

	FName Key;
	FCSCraftListRowSelected OnSelected;
};
