#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Features/Crafting/CSCraftStationActor.h"
#include "CSCraftSummonListWidget.generated.h"

class UButton;
class UCSCraftListRowWidget;
class UPanelWidget;
class UTextBlock;

// 마법진 E로 여는 창고 재료 목록. 여러 종류를 골라 한 번에 소환한다. 입력 잠금과 닫기는 제조대가 맡는다
// 줄을 누르면 그 줄이 +/- 대상이 되고, 처음 누른 줄은 1개로 담긴다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftSummonListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStation(ACSCraftStationActor* InStation);
	void RefreshList();
	void SelectProduct(FName ProductId);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// UI 입력만 받는 동안에는 IMC가 안 들어와서 Esc와 E를 여기서 받는다
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
	TObjectPtr<UButton> SummonButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SummonButtonText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CloseButtonText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftListRowWidget> RowClass;

	// 선택 요약 칸은 고르기 전 안내 문장과 고른 재료 이름을 번갈아 보여 줘서 서체를 바꿔 끼운다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	FName PlaceholderTypeface = TEXT("Info");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	FName SelectedTypeface = TEXT("Title");

private:
	UFUNCTION()
	void HandleCountUp();

	UFUNCTION()
	void HandleCountDown();

	UFUNCTION()
	void HandleSummon();

	UFUNCTION()
	void HandleClose();

	const FCSCraftStorageStock* FindStock(FName ProductId) const;
	int32 GetMaxCount(FName ProductId) const;
	void RefreshSelection();
	FText GetMessage(FName MessageRowName) const;

	TWeakObjectPtr<ACSCraftStationActor> Station;
	TArray<FCSCraftStorageStock> Stocks;
	// 담은 종류별 개수. +/-로 0이 되면 뺀다
	TMap<FName, int32> SelectedCounts;
	FName FocusedProductId;
};
