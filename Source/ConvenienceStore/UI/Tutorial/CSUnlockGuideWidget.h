#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSUnlockGuideWidget.generated.h"

class UBorder;
class UTextBlock;
class UButton;
class USizeBox;

USTRUCT(BlueprintType)
struct FCSUnlockGuideLayout
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout") FVector2D Anchor = FVector2D(1.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout") FVector2D Alignment = FVector2D(1.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout") FVector2D Position = FVector2D(-24.0f, 80.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout") bool bAutoSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (EditCondition = "!bAutoSize", ClampMin = "1.0")) FVector2D Size = FVector2D(560.0f, 300.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1.0")) float BodyHeight = 100.0f;
};

// 이 클래스를 부모로 WBP를 만드실 경우 아래 이름의 위젯을 그대로 배치해 주세요.
UCLASS()
class CONVENIENCESTORE_API UCSUnlockGuideWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCSUnlockGuideWidget(const FObjectInitializer& ObjectInitializer);
	void Present(const FText& Heading, const FText& Body, bool bCollapsed, bool bWaiting, bool bCamera, bool bSummary);
protected:
	// 끄시면 디자이너에서 지정하신 패널 위치와 크기, 본문 높이를 그대로 사용합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unlock Guide|Layout") bool bApplyLayoutSettings = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unlock Guide|Layout", meta = (EditCondition = "bApplyLayoutSettings")) FCSUnlockGuideLayout NotificationLayout;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unlock Guide|Layout", meta = (EditCondition = "bApplyLayoutSettings")) FCSUnlockGuideLayout SummaryLayout;
	// 버튼 문구를 직접 디자인하실 경우 끄시면 됩니다. 단축키 동작은 유지됩니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unlock Guide|Appearance") bool bUpdateButtonLabels = true;
	// 기본 표시 처리 후 호출되므로 장식과 버튼 내부 문구 등을 자유롭게 갱신하실 수 있습니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Unlock Guide")
	void OnGuidePresented(const FText& Heading, const FText& Body, bool bCollapsed, bool bWaiting, bool bCamera, bool bSummary);
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder> GuidePanel;
	// 두 패널을 모두 배치하시면 알림과 설명 팝업을 서로 다른 디자인으로 구성하실 수 있습니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> NotificationPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SummaryPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SummaryHeadingText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SummaryBodyText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USizeBox> SummaryBodyArea;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HeadingText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BodyText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USizeBox> BodyArea;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ViewButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> LaterButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> DismissButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CancelButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SkipButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CloseButton;
private:
	UFUNCTION() void View();
	UFUNCTION() void Later();
	UFUNCTION() void Dismiss();
	UFUNCTION() void Cancel();
	UFUNCTION() void Skip();
	UFUNCTION() void Close();
};
