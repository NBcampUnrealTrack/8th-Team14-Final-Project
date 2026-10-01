#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCraftTypes.h"
#include "CSCraftRecipeBookWidget.generated.h"

class ACSCraftStationActor;
class UCSCraftListRowWidget;
class UCSCraftingSubsystem;
class UImage;
class UPanelWidget;
class UTextBlock;
class UWidgetSwitcher;

// 클로즈업에서 Tab으로 여는 레시피 도감. 입력 잠금과 닫기는 제조대가 맡는다
// 열려 있는 동안에는 제조가 진행되지 않아서 열 때 한 번만 그린다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftRecipeBookWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStation(ACSCraftStationActor* InStation);
	void RefreshList();
	void SelectEntry(FName EntryKey);

protected:
	virtual void NativeConstruct() override;
	// Tab은 일반 KeyDown 전에 포커스 이동으로 쓰일 수 있어 미리 받는다
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	// 나머지 키를 삼키지 않으면 A/D가 창 밖으로 새어 불 조절이 된다
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> RecipeList;

	// 0 = 레시피 상세, 1 = 최근 시도
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> DetailSwitcher;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailTitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailRecipeText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorMaterialLabelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorMaterialText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorHeatLabelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorHeatText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorStirLabelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RumorStirText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> AttemptList;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> DetailIconImage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftListRowWidget> RowClass;

private:
	const UCSCraftingSubsystem* GetCrafting() const;
	void ShowRecipeDetail(FName RecipeRowName);
	void ShowRecentAttempts();
	void SetRumorVisible(bool bVisible);
	FText GetHeatName(ECSCraftHeat Heat) const;
	FText FormatIngredientLine(FName MessageRowName, const FFormatNamedArguments& Extra,
		FName IngredientA, int32 CountA, FName IngredientB, int32 CountB, ECSCraftHeat Heat, int32 Stir) const;
	UCSCraftListRowWidget* AddRow(UPanelWidget* Panel, FName Key, const FText& Name, const FText& Detail,
		FName IconProductId, bool bSelectable);

	TWeakObjectPtr<ACSCraftStationActor> Station;
};
