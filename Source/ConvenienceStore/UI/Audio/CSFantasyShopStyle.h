#pragma once
#include "CoreMinimal.h"
#include "CSFantasyShopStyle.generated.h"

class UTexture2D;
class UFont;
class UWidgetTree;
class UVerticalBox;
class UTextBlock;
class UButton;
class USizeBox;
class UBorder;

// 두 선택창이 기존 프로젝트의 패널/버튼/한글 폰트를 공유합니다.
USTRUCT()
struct FCSFantasyShopStyle
{
	GENERATED_BODY()
	FCSFantasyShopStyle();
	UPROPERTY() TObjectPtr<UTexture2D> Panel;
	UPROPERTY() TObjectPtr<UTexture2D> Button;
	UPROPERTY() TObjectPtr<UTexture2D> SelectedButton;
	UPROPERTY() TObjectPtr<UFont> Font;
};

namespace CSFantasyShop
{
	UTextBlock* Text(UWidgetTree* Tree, const FCSFantasyShopStyle& Style, const FText& Value, int32 Size=20);
	void Button(UButton* Button, const FCSFantasyShopStyle& Style, bool bSelected=false);
	USizeBox* ButtonSize(UWidgetTree* Tree, UButton* Button, float Width=220.f);
	UBorder* ListFrame(UWidgetTree* Tree, const FCSFantasyShopStyle& Style, bool bOuterFrame=false);
	void SetPanelSize(UWidgetTree* Tree, FVector2D Size);
	UVerticalBox* Body(UWidgetTree* Tree, const FCSFantasyShopStyle& Style, const FText& Title, const FText& Hint, FVector2D Size, UTextBlock** OutHint=nullptr);
}
