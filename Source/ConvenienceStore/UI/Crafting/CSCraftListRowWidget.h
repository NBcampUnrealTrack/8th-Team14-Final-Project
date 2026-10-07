#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "CSCraftListRowWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;

DECLARE_DELEGATE_OneParam(FCSCraftListRowSelected, FName);

// 소환 목록, 자동 제조 창, 도감이 같이 쓰는 한 줄. 어느 창에 붙는지는 선택 콜백으로만 안다
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

	void SetAttemptPresentation(const FText& ResultLine, const FText& FailureDetail, bool bSucceeded);
	void SetAutoRecipePresentation();
	// 고른 줄은 버튼의 Hovered 모양을 계속 쓴다
	void SetSelected(bool bInSelected);
	FName GetKey() const { return Key; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 최근 시도 카드. 공유 줄의 이름 칸을 기록 문장으로 쓰므로 물품 이름 서체 대신 본문 서체로 바꾼다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FName AttemptTypeface = TEXT("Info");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptNameColor = FSlateColor(FLinearColor::FromSRGBColor(FColor(61, 54, 39)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptDetailColor = FSlateColor(FLinearColor::FromSRGBColor(FColor(118, 100, 71)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptSuccessColor = FSlateColor(FLinearColor::FromSRGBColor(FColor(66, 104, 48)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptFailureColor = FSlateColor(FLinearColor::FromSRGBColor(FColor(156, 60, 43)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptSuccessCardTint = FSlateColor(FLinearColor::FromSRGBColor(FColor(235, 242, 220)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptFailureCardTint = FSlateColor(FLinearColor::FromSRGBColor(FColor(248, 231, 219)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptSuccessOutline = FSlateColor(FLinearColor::FromSRGBColor(FColor(158, 177, 131)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt")
	FSlateColor AttemptFailureOutline = FSlateColor(FLinearColor::FromSRGBColor(FColor(194, 148, 129)));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt", meta = (ClampMin = "0.0"))
	float AttemptOutlineWidth = 0.8f;

	// 결과 줄이 개수 칸 대신 설명 아래로 내려갈 때 위 간격
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|Attempt", meta = (ClampMin = "0.0"))
	float AttemptResultGap = 6.0f;

	// 자동 제조 줄은 재료가 두 줄이라 위아래를 더 띄운다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|AutoRecipe")
	FMargin AutoRecipeNormalPadding = FMargin(32.0f, 14.0f, 24.0f, 14.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|AutoRecipe")
	FMargin AutoRecipePressedPadding = FMargin(32.0f, 15.0f, 24.0f, 13.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI|AutoRecipe", meta = (ClampMin = "0.0"))
	float AutoRecipeDetailGap = 8.0f;

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

	// WBP에 정한 버튼 모양을 바탕으로 선택·용도별 변형을 다시 입힌다
	void ApplyButtonStyle();

	FName Key;
	FCSCraftListRowSelected OnSelected;
	FButtonStyle BaseStyle;
	bool bBaseStyleCached = false;
	bool bSelected = false;
	bool bAutoRecipeLayout = false;
	bool bAttemptCard = false;
	bool bAttemptSucceeded = false;
};
