#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSCraftIngredientPanelWidget.generated.h"

class ACSCraftStationActor;
class UButton;
class UCSCraftIngredientRowWidget;
class UPanelWidget;
class UTextBlock;

// 클로즈업에서 E로 여는 솥 재료 창. 입력 잠금과 닫기는 제조대가 맡는다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftIngredientPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStation(ACSCraftStationActor* InStation);
	void RefreshList();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// 나머지 키를 삼키지 않으면 A/D가 창 밖으로 새어 불 조절이 된다
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> RowList;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CloseButtonText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftIngredientRowWidget> RowClass;

private:
	UFUNCTION()
	void HandleClose();

	void HandleRemove(FName ProductId);

	TWeakObjectPtr<ACSCraftStationActor> Station;
};
