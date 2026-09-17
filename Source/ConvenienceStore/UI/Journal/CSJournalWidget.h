#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSJournalViewTypes.h"
#include "CSJournalWidget.generated.h"

class UButton;
class UWidgetSwitcher;
class UTextBlock;
class UCSJournalSubsystem;
class ACSJournalMapSettings;

// 화면은 WBP 디자이너에서 제작하며 C++에서는 상태, 데이터 조회와 입력 처리를 제공합니다.
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSJournalWidget : public UUserWidget
{
	GENERATED_BODY()
	friend class FCSJournalPresentationTest;

public:
	// X 버튼은 이 함수를 호출합니다. Tab과 Esc도 동일한 닫기 경로를 사용합니다.
	UFUNCTION(BlueprintCallable, Category = "Journal|Navigation") void RequestClose();
	UFUNCTION(BlueprintCallable, Category = "Journal|Navigation") void SelectPage(ECSJournalPage NewPage);
	UFUNCTION(BlueprintCallable, Category = "Journal|Navigation") void NextPage();
	UFUNCTION(BlueprintCallable, Category = "Journal|Navigation") void PreviousPage();
	// 월 인덱스는 0부터 시작하며 지나온 마지막 달까지만 선택하실 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "Journal|Calendar") bool SelectMonth(int32 NewMonthIndex);
	UFUNCTION(BlueprintCallable, Category = "Journal|Calendar") bool SelectDay(int32 BusinessDay);
	UFUNCTION(BlueprintCallable, Category = "Journal|Artifacts") bool SelectArtifact(FName ArtifactId);
	UFUNCTION(BlueprintCallable, Category = "Journal") void RefreshJournal();

	UFUNCTION(BlueprintPure, Category = "Journal|Navigation") FText GetPageTitle() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Navigation") bool GetSelectedRace(ECSJournalRace& OutRace) const;
	UFUNCTION(BlueprintPure, Category = "Journal|Calendar") int32 GetCurrentBusinessDay() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Calendar") int32 GetLatestMonthIndex() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Calendar") static FCSJournalDateView GetDateInfo(int32 BusinessDay);
	UFUNCTION(BlueprintPure, Category = "Journal|Calendar") TArray<FCSJournalCalendarDayView> GetCalendarDays() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Calendar") TArray<FCSJournalEvent> GetSelectedDayEvents() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Races")
	TArray<FCSJournalRaceEntryView> GetRaceEntries(ECSJournalRace Race) const;
	UFUNCTION(BlueprintPure, Category = "Journal|Artifacts") TArray<FCSJournalArtifactView> GetArtifactEntries() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Artifacts")
	bool GetSelectedArtifact(FCSJournalArtifactView& OutArtifact) const;
	UFUNCTION(BlueprintPure, Category = "Journal|Delivery")
	TArray<FCSDeliveryOrderRuntime> GetAcceptedDeliveryOrders() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Delivery")
	TArray<FCSJournalDelivery> GetFinishedDeliveryHistory() const;
	UFUNCTION(BlueprintPure, Category = "Journal|Delivery") float GetRemainingDeliveryMinutes(int32 OrderId) const;
	UFUNCTION(BlueprintPure, Category = "Journal|Map") FCSJournalMapView GetMapData() const;

	UPROPERTY(BlueprintReadOnly, Category = "Journal|Navigation") ECSJournalPage SelectedPage = ECSJournalPage::Map;
	UPROPERTY(BlueprintReadOnly, Category = "Journal|Calendar") int32 SelectedMonthIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Journal|Calendar") int32 SelectedBusinessDay = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Journal|Artifacts") FName SelectedArtifactId;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Journal") TObjectPtr<UCSJournalSubsystem> Journal;

protected:
 // 디자이너 기반 기본 화면의 표시를 갱신합니다. 사용자 WBP 이벤트보다 먼저 호출됩니다.
 virtual void ApplyJournalView() {}
 virtual void ApplyMapView(const FCSJournalMapView& Data) {}
 virtual void ApplyDeliveryClock() {}
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	// 해당 이름의 위젯이 있으면 C++에서 자동 연결합니다. 없어도 사용자 WBP 이벤트로 구성하실 수 있습니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> NextPageButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> PreviousPageButton;
	// Map부터 Artifacts까지 열 개의 자식 페이지를 enum 순서대로 배치해 주시면 됩니다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidgetSwitcher> PageSwitcher;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PageTitleText;

	// 최초 구성 및 기록·선택 상태 변경 시 호출됩니다. 현재 페이지의 목록을 갱신해 주시면 됩니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Journal|Events") void OnJournalUpdated();
	UFUNCTION(BlueprintImplementableEvent, Category = "Journal|Events")
	void OnJournalPageChanged(ECSJournalPage NewPage);
	// 지도 구역에서만 주기적으로 호출됩니다. 지도 배경은 유지하고 화살표 위치를 갱신해 주시면 됩니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Journal|Events")
	void OnJournalMapUpdated(const FCSJournalMapView& MapData);
	// 배달 구역에서만 주기적으로 호출됩니다. 남은 시간 텍스트를 갱신해 주시면 됩니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Journal|Events")
	void OnJournalDeliveryClockUpdated(float GameMinutes);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Journal", meta = (ClampMin = "0.05"))
	float LiveUpdateInterval = 0.2f;

private:
	void CacheMapGeometry();
	void UpdateLiveData();
	void UpdateNavigationWidgets();
	FVector2D WorldToMap(FVector Position) const;
	UPROPERTY(Transient) TObjectPtr<ACSJournalMapSettings> MapSettings;
	UPROPERTY(Transient) FCSJournalMapView CachedMap;
	FBox MapBounds = FBox(ForceInit);
	FTimerHandle LiveUpdateTimer;
	bool bRefreshing = false;
};
