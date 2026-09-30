#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSTutorialTypes.h"
#include "CSCraftKeyHintRowWidget.generated.h"

class UTextBlock;

// 클로즈업 키 안내 한 줄
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCraftKeyHintRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetEntry(const FCSTutorialHintEntry& Entry);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HintText;
};
