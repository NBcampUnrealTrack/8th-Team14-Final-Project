#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSNoticeEventTypes.h"
#include "CSNoticeBoardNewsTabWidget.generated.h"

class UCSNoticeEventSubsystem;
class UScrollBox;

/** 오늘의 뉴스와 연결된 수요 변화 Row들을 게시판 뉴스 탭에 표시합니다. */
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSNoticeBoardNewsTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Notice Board|News")
	void RefreshNewsTab();

	/** 제목·본문·뉴스 없음 상태처럼 디자인에 종속된 영역은 WBP에서 갱신합니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|News")
	void RefreshNewsHeaderView(const FCSNewsEventDataRow& News, bool bHasNews);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	/** 이름을 맞추면 자동 연결됩니다. 기존 ScrollBox_63도 임시 호환합니다. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> ScrollBox_DemandChanges;

	/** 비워두면 프로젝트의 WBP_NoticeBoardNewsDemandRow를 자동으로 사용합니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Notice Board|News")
	TSubclassOf<UUserWidget> DemandRowWidgetClass;

private:
	UFUNCTION()
	void HandleNoticeEventsChanged();

	UScrollBox* ResolveDemandScrollBox() const;
	TSubclassOf<UUserWidget> ResolveDemandRowClass() const;
	void PopulateDemandRows(const TArray<FCSNoticeBoardDemandChange>& Changes);
	void SetupDemandRow(UUserWidget* RowWidget, const FCSNoticeBoardDemandChange& Change) const;

	UPROPERTY(Transient)
	TObjectPtr<UCSNoticeEventSubsystem> NoticeEventSubsystem;
};
