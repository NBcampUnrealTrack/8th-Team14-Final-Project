#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSLedgerCraftInfoWidget.generated.h"

class UCSCraftingSubsystem;
class UTextBlock;

// 장부 오른쪽 페이지에 끼워 넣는 제조 상품 안내 칸. 장부가 상품을 고를 때마다 불러 준다
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSLedgerCraftInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 아직 만들어 본 적 없는 상품에 도감과 게시판을 안내한다
	void ShowGuide();
	void HideInfo();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GuideTitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GuideBodyText1;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GuideBodyText2;

private:
	const UCSCraftingSubsystem* GetCrafting() const;
};
