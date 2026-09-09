#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSNoticeBoardWidget.generated.h"

class ACSNoticeBoardActor;

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

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Notice Board")
	TObjectPtr<ACSNoticeBoardActor> NoticeBoardActor;

	UPROPERTY(BlueprintReadOnly, Category = "Notice Board")
	FName SelectedTabId = TEXT("Weather");
};
