#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Features/Crafting/CSCraftStationActor.h"
#include "CSCraftAutoPanelWidget.generated.h"

class UButton;
class UCSCraftListRowWidget;
class UPanelWidget;
class UTextBlock;

// 클로즈업에서 R로 여는 자동 제조 창. 입력 잠금과 닫기는 제조대가 맡는다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftAutoPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStation(ACSCraftStationActor* InStation);
	void RefreshList();
	void SelectRecipe(FName RecipeRowName);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// 나머지 키를 삼키지 않으면 A/D가 창 밖으로 새어 불 조절이 된다
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> RowList;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SelectedNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CountUpButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CountDownButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CraftButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CraftButtonText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CloseButtonText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftListRowWidget> RowClass;

private:
	UFUNCTION()
	void HandleCountUp();

	UFUNCTION()
	void HandleCountDown();

	UFUNCTION()
	void HandleCraft();

	UFUNCTION()
	void HandleClose();

	const FCSCraftAutoOption* FindSelectedOption() const;
	void RefreshSelection();

	TWeakObjectPtr<ACSCraftStationActor> Station;
	TArray<FCSCraftAutoOption> Options;
	FName SelectedRecipeRowName;
	int32 SelectedSets = 1;
};
