#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSNoticeBoardWidget.generated.h"

class ACSNoticeBoardActor;
class UButton;
class UTextBlock;
class UWidget;

/**
 * 상호작용 뒤 화면에 표시되는 게시판의 공통 UMG 기반 클래스입니다.
 * 날씨·유행·이벤트·배달 탭의 실제 레이아웃과 전환은 블루프린트에서 구성합니다.
 */
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSNoticeBoardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Notice Board")
	void InitializeNoticeBoard(ACSNoticeBoardActor* InNoticeBoardActor);

	UFUNCTION(BlueprintPure, Category = "Notice Board")
	ACSNoticeBoardActor* GetNoticeBoardActor() const { return NoticeBoardActor; }

	/** 닫기 버튼에 연결합니다. */
	UFUNCTION(BlueprintCallable, Category = "Notice Board")
	void CloseNoticeBoardWindow();

	/** 탭 버튼에 연결할 공통 진입점입니다. 예: Weather, Trend, Event, Delivery */
	UFUNCTION(BlueprintCallable, Category = "Notice Board")
	void SelectTab(FName TabId);

	/** 위젯이 처음 열릴 때 헤더·초기 탭을 구성합니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board")
	void InitializeNoticeBoardView();

	/** SelectTab 호출 시 블루프린트에서 WidgetSwitcher와 탭 버튼 상태를 갱신합니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board")
	void ShowNoticeBoardTab(FName TabId);

	virtual void NativeDestruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Notice Board")
	TObjectPtr<ACSNoticeBoardActor> NoticeBoardActor;

	UPROPERTY(BlueprintReadOnly, Category = "Notice Board")
	FName SelectedTabId = TEXT("Weather");

	/** 장부 UI와 동일하게 선택 배경은 별도 위젯으로 표시하고 버튼은 클릭·호버만 담당합니다. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Button_Close;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Button_Weather;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Button_Trend;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Button_Event;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Button_Delivery;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Weather;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Trend;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Event;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SelBg_Delivery;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_WeatherTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_WeatherDescription;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_TrendTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_TrendDescription;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_EventTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_EventDescription;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_DeliveryTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Txt_DeliveryDescription;

	/** 실제 색은 WBP_NoticeBoardWidget의 클래스 기본값에서 지정합니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Tab Style")
	FSlateColor DefaultTabTitleColor = FSlateColor(FLinearColor(0.22f, 0.15f, 0.08f, 1.0f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Tab Style")
	FSlateColor DefaultTabDescriptionColor = FSlateColor(FLinearColor(0.42f, 0.36f, 0.28f, 1.0f));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Tab Style")
	FSlateColor HighlightedTabTitleColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Tab Style")
	FSlateColor HighlightedTabDescriptionColor = FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f));

	/** 게시판이 열릴 때 아래에서 올라오며 나타나는 시간입니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Animation", meta = (ClampMin = "0.0"))
	float OpenAnimationDuration = 0.3f;

	/** 게시판이 내려간 뒤 실제로 닫히기까지의 시간입니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Animation", meta = (ClampMin = "0.0"))
	float CloseAnimationDuration = 0.22f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Animation", meta = (ClampMin = "0.0"))
	float AnimationVerticalOffset = 48.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|Animation", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float AnimationStartScale = 0.96f;

private:
	enum class ENoticeBoardAnimationState : uint8
	{
		Opening,
		Idle,
		Closing
	};

	FName HoveredTabId = NAME_None;
	ENoticeBoardAnimationState AnimationState = ENoticeBoardAnimationState::Opening;
	float AnimationElapsedTime = 0.0f;
	float CurrentAnimationAlpha = 0.0f;
	float ClosingStartAlpha = 1.0f;

	void RefreshTabPresentations();
	void ApplyWindowAnimation(float Alpha);
	void FinishClosing();
	void RefreshTabPresentation(
		FName TabId,
		UWidget* SelectedBackground,
		UTextBlock* TitleText,
		UTextBlock* DescriptionText) const;

	UFUNCTION() void HandleWeatherHovered();
	UFUNCTION() void HandleWeatherUnhovered();
	UFUNCTION() void HandleWeatherClicked();
	UFUNCTION() void HandleTrendHovered();
	UFUNCTION() void HandleTrendUnhovered();
	UFUNCTION() void HandleTrendClicked();
	UFUNCTION() void HandleEventHovered();
	UFUNCTION() void HandleEventUnhovered();
	UFUNCTION() void HandleEventClicked();
	UFUNCTION() void HandleDeliveryHovered();
	UFUNCTION() void HandleDeliveryUnhovered();
	UFUNCTION() void HandleDeliveryClicked();

	void SetHoveredTab(FName TabId, bool bHovered);
};
