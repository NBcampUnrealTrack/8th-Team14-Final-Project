#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/HUD/CSInteractionPrompt.h"
#include "CSInteractionPanelWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;

/*
상호작용 UI 사용 방법
- 게임플레이 코드는 CSStorePlayerController의 SetInteractionPrompt 등 전용 함수를 호출합니다.
- 문구, 표시 여부, Text 색상, 일반/위험 배경 선택은 이 C++ 위젯이 담당합니다.
- Normal, Warning, Danger, Price의 실제 Text 색상과 두 배경은 WBP_InteractionPanel Class Defaults에서 지정합니다.
- 새 정보 행을 추가할 때는 Row, Background, Text를 같은 패널에 만들고 전용 setter를 추가합니다.
- WBP에서는 SetText나 상태 판정을 만들지 않고 배치와 디자인만 관리합니다.
*/
// 상호작용 정보의 문구, 색상, 배경과 행 표시 여부를 한곳에서 관리합니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSInteractionPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetInteractionPrompt(const FText& Message, const FLinearColor& RequestedColor);
	void SetInteractionPrompts(const TArray<FCSInteractionPrompt>& Prompts);
	void SetProductPrice(const FText& Message);
	void SetProductShelfLife(const FText& Message, const FLinearColor& RequestedColor);
	void SetProductFreshness(const FText& Message, const FLinearColor& RequestedColor);
	void SetStorageWarning(const FText& Message);
	void ClearInteractionContext();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> InteractionPromptRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ProductPriceRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ProductShelfLifeRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ProductFreshnessRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> StorageWarningRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> InteractionPromptBackground;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ProductPriceBackground;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ProductShelfLifeBackground;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ProductFreshnessBackground;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> StorageWarningBackground;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InteractionPromptText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProductPriceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProductShelfLifeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProductFreshnessText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StorageWarningText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Style")
	TObjectPtr<UTexture2D> NormalBackgroundTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Style")
	TObjectPtr<UTexture2D> DangerBackgroundTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Style")
	FLinearColor NormalTextColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Style")
	FLinearColor WarningTextColor = FLinearColor(1.0f, 0.8f, 0.1f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Style")
	FLinearColor DangerTextColor = FLinearColor(1.0f, 0.25f, 0.25f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction|Style")
	FLinearColor PriceTextColor = FLinearColor(1.0f, 0.85f, 0.35f, 1.0f);

private:
	// 추가 안내는 기존 안내와 같은 배경과 글꼴을 사용하는 독립 행에 표시합니다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> AdditionalPromptRows;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> AdditionalPromptBackgrounds;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> AdditionalPromptTexts;

	enum class EInteractionVisualStyle : uint8
	{
		Normal,
		Warning,
		Danger,
		Price
	};

	EInteractionVisualStyle ResolveRequestedStyle(const FLinearColor& RequestedColor) const;
	void SetRow(
		UWidget* Row,
		UImage* Background,
		UTextBlock* TextBlock,
		const FText& Message,
		EInteractionVisualStyle Style);
	FLinearColor GetTextColor(EInteractionVisualStyle Style) const;
};
