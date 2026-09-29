#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSCraftTypes.h"
#include "CSCraftCloseupWidget.generated.h"

class UPanelWidget;
class UCSCraftKeyHintRowWidget;

// 솥 클로즈업 중 하단 키 안내. 입력은 받지 않는다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftCloseupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 목록을 비우고 그 단계의 키 안내를 다시 채운다
	void SetStep(ECSCraftStep Step);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> KeyHintList;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftKeyHintRowWidget> KeyHintRowClass;
};
