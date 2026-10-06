#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Layout/Visibility.h"
#include "GameFramework/PlayerController.h"
#include "Data/CSStorePhaseTypes.h"
#include "UI/HUD/CSInteractionPrompt.h"
#include "CSStorePlayerController.generated.h"

class UCSJournalWidget;
class UCSSettingsWidget;
class ACSStoreGameState;
class UCSStoreHUDWidget;
class UCSStoreTopBarWidget;
class UUserWidget;
class UCSDailySummaryWidget;
class UCSStatusAnnouncementWidget;
class UCSClosingWarningWidget;
class UCSDeliveryToastWidget;
class UCSNoticeBannerWidgetBase;
class UCSSaveLoadMenuWidget;
class UTexture2D;
class UInputAction;
class UInputMappingContext;
class ACSStoreOpeningDirector;
enum class ECSStatusAnnouncementType : uint8;

// 발주/계산/장부처럼 게임 조작을 막는 UI가 열리거나 닫힐 때마다 방송
// 튜토리얼이 어떤 UI가 열렸는지 감지하는 경로
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnGameplayUIInputLockChanged,
	UUserWidget*,
	RequestingWidget,
	bool,
	bLocked);

struct FCSQueuedDeliveryToast
{
	FText Title;
	FText Message;
};

// 개점·마감 안내(WBP_StatusAnnouncement) 애니메이션이 끝났을 때 방송
// 튜토리얼이 고정 시간 대신 이 시점에 맞춰 다음 안내를 띄우는 데 씀
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnStoreAnnouncementFinished);

// 로컬 플레이어의 편의점 HUD와 마감 UI 입력 상태를 관리합니다.
UCLASS()
class CONVENIENCESTORE_API ACSStorePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UI|Summary")
	void ToggleDailySummary();
	bool IsDailySummaryReadOnly() const { return bDailySummaryReadOnly; }

	UFUNCTION(BlueprintCallable, Category = "Journal")
	void ToggleJournal();
	UFUNCTION(BlueprintCallable, Category = "Journal")
	void CloseJournal();
	// 일시정지된 요정 연출에서도 카메라를 갱신하고, 반환값으로 이전 설정을 복원해 주세요.
	bool SetFairyIntroductionTickWhenPaused(bool bEnabled)
	{
		const bool bPrevious = bShouldPerformFullTickWhenPaused;
		bShouldPerformFullTickWhenPaused = bEnabled;
		return bPrevious;
	}

	UFUNCTION(BlueprintCallable, Category = "UI|Settings")
	void ToggleSettings();
	UFUNCTION(BlueprintCallable, Category = "UI|Settings")
	void CloseSettings();
	// 임시 타이틀의 시작 버튼에서 호출합니다. 정식 타이틀 도입 시 이 구역 전체를 제거할 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "UI|Temporary Title")
	void StartGameFromTemporaryTitle();

	// ----- 세이브 / 로드 -----

	// 저장·불러오기 메뉴를 열거나 닫음
	UFUNCTION(BlueprintCallable, Category = "UI|Save")
	void ToggleSaveLoadMenu();

	// 열려 있는 저장·불러오기 메뉴를 닫고 입력 잠금과 HUD 숨김을 해제
	UFUNCTION(BlueprintCallable, Category = "UI|Save")
	void CloseSaveLoadMenu();

	// 발주·계산처럼 게임 조작을 막아야 하는 UI가 열리고 닫힐 때 입력 잠금을 갱신합니다.
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetGameplayUIInputLock(UUserWidget* RequestingWidget, bool bShouldLock);

	// 계산·정산처럼 상시 HUD를 가려야 하는 UI의 요청을 등록하거나 해제합니다.
	// 요청이 모두 해제된 뒤에만 HUD를 이전 표시 상태로 복원합니다.
	UFUNCTION(BlueprintCallable, Category = "UI|HUD")
	void SetStoreHUDHidden(UObject* Requester, bool bShouldHide);

	// 타이틀과 엔딩처럼 게임 화면을 완전히 덮는 장면에서만 상단 바를 숨깁니다.
	void SetStoreTopBarHidden(UObject* Requester, bool bShouldHide);

	// 계산·발주 등 게임 조작을 막는 UI가 현재 열려 있는지 반환합니다.
	bool IsGameplayUIInputLocked() const;

	// C, Tab, P는 BindKey라 클로즈업 IMC에 막히지 않아서 이 값으로 거른다
	void SetCraftCloseupActive(bool bActive) { bCraftCloseupActive = bActive; }
	bool IsCraftCloseupActive() const { return bCraftCloseupActive; }

	// 수면 중에도 정산 데이터 이벤트는 유지하고, 중간 정산 UI만 생략합니다.
	bool BeginSleepTransition(AActor* Requester);
	void EndSleepTransition(AActor* Requester);
	bool IsSleepTransitionActive() const { return IsValid(SleepTransitionOwner); }
	void SetSleepTransitionWidget(UUserWidget* Widget);
	void ShowSleepDailySummary();

	// 진열할 빈 슬롯이 없을 때 HUD에 빨간 배치 불가 안내를 표시합니다.
	void ShowPlacementUnavailablePrompt();

	// 조준한 대상의 상호작용 안내를 HUD에 전달합니다.
	void SetInteractionPrompt(const FText& Message, const FLinearColor& TextColor);
	void SetInteractionPrompts(const TArray<FCSInteractionPrompt>& Prompts);

	// 조준한 상품의 판매가를 HUD에 전달합니다.
	void SetProductPrice(const FText& Message);

	// 조준한 개별 상품의 남은 유통기한과 표시 색상을 HUD에 전달합니다.
	void SetProductShelfLife(const FText& Message, const FLinearColor& TextColor);

	// 냉장·냉동 상품의 현재 신선도와 표시 색상을 HUD에 전달합니다.
	void SetProductFreshness(const FText& Message, const FLinearColor& TextColor);

	// 진열 보관 환경이 맞지 않을 때 HUD에 경고를 전달합니다.
	void SetStorageWarning(const FText& Message);

	/** 배달 기한 초과처럼 입력을 막으면 안 되는 결과를 화면 토스트로 표시합니다. */
	void ShowGameplayToast(const FText& Title, const FText& Message);

	// 화면 중앙에 잠깐 떴다 사라지는 경고 배너를 표시합니다. 튜토리얼 진행 상태와 무관하게 동작하며
	// 인스턴스 하나를 재사용하므로 어느 파트에서 호출해도 됩니다 (NoticeBannerWidgetClass는 사람이 지정)
	void ShowNoticeBanner(const FText& Message);

	// 배치 대상이 바뀌거나 상품을 내려놓았을 때 HUD의 조준 안내를 지웁니다.
	void ClearInteractionContext();

	// 마법봉처럼 도구 전용 조준점을 띄우는 동안 기본 조준점을 숨기거나 되돌립니다.
	void SetDefaultCrosshairVisible(bool bVisible);

	// 캐릭터의 건축 모드 상태를 상시 HUD에 전달합니다.
	void SetBuildModeHUDState(bool bIsBuildMode, bool bPlayTransition);

	// 플레이어 퀵바의 아이콘과 현재 선택 슬롯을 HUD에 전달합니다.
	void SetQuickbarHUDData(
		int32 SlotCount,
		int32 SelectedSlotIndex,
		const TArray<FText>& ToolNames,
		const TArray<UTexture2D*>& ToolIcons);

	// UI 입력 잠금이 걸리거나 풀릴 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "UI|Input")
	FCSOnGameplayUIInputLockChanged OnGameplayUIInputLockChanged;

	// "영업을 시작하였습니다" 안내 애니메이션이 끝났을 때 방송
	UPROPERTY(BlueprintAssignable, Category = "UI|Announcement")
	FCSOnStoreAnnouncementFinished OnStoreOpenedAnnouncementFinished;

	// "영업시간이 종료되었습니다" 안내 애니메이션이 끝났을 때 방송
	UPROPERTY(BlueprintAssignable, Category = "UI|Announcement")
	FCSOnStoreAnnouncementFinished OnStoreClosedAnnouncementFinished;

protected:
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	// UI 담당자가 제작한 CSJournalWidget 기반 WBP를 지정해 주시면 됩니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Journal")
	TSubclassOf<UCSJournalWidget> JournalWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSJournalWidget> ActiveJournalWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Settings")
	TSubclassOf<UCSSettingsWidget> SettingsWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSSettingsWidget> ActiveSettingsWidget;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleEndOfDayTaskChanged(ECEndOfDayTask NewTask);

	UFUNCTION()
	void HandleStorePhaseChanged(ECStorePhase NewPhase);
	UFUNCTION() void HandleStatusAnnouncementFinished();
	UFUNCTION() void HandleDailySummaryRequested();
	UFUNCTION() void HandleDailySummaryConfirmed();
	UFUNCTION() void HandleClosingWarningRequested(int32 RemainingMinutes);
	UFUNCTION() void HandleClosingWarningFinished();
	UFUNCTION() void HandleDeliveryToastFinished();

	void UpdateClosingInputMode();
	void ShowDailySummary(bool bReadOnly);
	void ShowStatusAnnouncement(ECSStatusAnnouncementType AnnouncementType);
	void TryShowPendingClosingAnnouncement();
	void TryShowQueuedDeliveryToast();
	bool IsCriticalDeliveryToastUIOpen() const;
	bool HasGameplayUIInputLock() const;
	UUserWidget* GetFocusedGameplayUIWidget() const;

	// === Temporary Title Screen: 정식 타이틀 도입 시 이 구역과 BeginPlay 호출 한 줄 제거 ===
	void ShowTemporaryTitleScreen();
	void CloseTemporaryTitleScreen();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Temporary Title")
	bool bUseTemporaryTitleScreen = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Temporary Title")
	TSubclassOf<UUserWidget> TemporaryTitleWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ActiveTemporaryTitleWidget;
	// === End Temporary Title Screen ===

	// 연출 액터를 배치하지 않으시면 시작 위치를 기준으로 기본 전경 카메라를 생성합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Opening")
	bool bUseOpeningPresentation = true;
	UPROPERTY(Transient)
	TObjectPtr<ACSStoreOpeningDirector> ActiveOpeningDirector;
	bool bOpeningPresentationActive = false;
	bool bOpeningPresentationPlayed = false;
	bool bOpeningPresentationRequested = false;
	FTimerHandle OpeningStartTimer;
	TWeakPtr<SWidget> OpeningUILayer;
	EVisibility UILayerVisibilityBeforeOpening = EVisibility::SelfHitTestInvisible;
	void SetOpeningUIHidden(bool bShouldHide);
	void StartOpeningPresentation();
	UFUNCTION() void FinishOpeningPresentation();

	// 메뉴 위젯의 닫기 요청을 처리
	UFUNCTION()
	void HandleSaveLoadMenuClosed();

	// 저장·불러오기 메뉴 위젯 클래스
	// 지정하지 않으면 메뉴가 열리지 않음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Save")
	TSubclassOf<UCSSaveLoadMenuWidget> SaveLoadMenuWidgetClass;

	// 현재 열려 있는 저장·불러오기 메뉴
	UPROPERTY(Transient)
	TObjectPtr<UCSSaveLoadMenuWidget> ActiveSaveLoadMenuWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UCSStoreHUDWidget> StoreHUDWidgetClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Top Bar")
	TSubclassOf<UCSStoreTopBarWidget> StoreTopBarWidgetClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Closing")
	TSubclassOf<UCSDailySummaryWidget> DailySummaryWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UCSDailySummaryWidget> ActiveDailySummaryWidget;
	bool bDailySummaryReadOnly = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Announcement")
	TSubclassOf<UCSStatusAnnouncementWidget> StatusAnnouncementWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSStatusAnnouncementWidget> ActiveStatusAnnouncementWidget;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Closing Warning")
	TSubclassOf<UCSClosingWarningWidget> ClosingWarningWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSClosingWarningWidget> ActiveClosingWarningWidget;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Delivery Toast")
	TSubclassOf<UCSDeliveryToastWidget> DeliveryToastWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryToastWidget> ActiveDeliveryToastWidget;
	TArray<FCSQueuedDeliveryToast> QueuedDeliveryToasts;

	// WBP_TutorialBanner를 복제해 UCSNoticeBannerWidgetBase로 리페어런트한 WBP를 지정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Notice Banner")
	TSubclassOf<UCSNoticeBannerWidgetBase> NoticeBannerWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSNoticeBannerWidgetBase> ActiveNoticeBannerWidget;

	UPROPERTY()
	TObjectPtr<UCSStoreHUDWidget> StoreHUDWidget;
	UPROPERTY(Transient)
	TObjectPtr<UCSStoreTopBarWidget> StoreTopBarWidget;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> StoreTopBarHideRequesters;

	// HUD 생성 순서와 무관하게 퀵바 초기 상태를 전달하기 위한 캐시입니다.
	int32 CachedQuickbarSlotCount = 0;
	int32 CachedQuickbarSelectedSlotIndex = 0;
	TArray<FText> CachedQuickbarToolNames;
	TArray<TObjectPtr<UTexture2D>> CachedQuickbarToolIcons;

	// 첫 숨김 요청 전 HUD 표시 상태를 마지막 요청 해제 시 정확히 복구합니다.
	ESlateVisibility StoreHUDVisibilityBeforeHide =
		ESlateVisibility::HitTestInvisible;
	bool bHasStoredHUDVisibilityBeforeHide = false;

	// 계산·정산이 전환 과정에서 겹쳐도 HUD가 먼저 나타나지 않도록 요청자를 누적합니다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> StoreHUDHideRequesters;

	// 현재 게임 조작을 막아야 하는 발주·계산 UI 목록입니다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UUserWidget>> GameplayUIInputLockWidgets;
	UPROPERTY(Transient)
	TObjectPtr<AActor> SleepTransitionOwner;
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ActiveSleepTransitionWidget;

	// 설정창 Esc를 Enhanced Input으로 받아야 솥 클로즈업의 Esc가 먼저 가져가서
	// 클로즈업을 나가면서 설정창까지 열리지 않는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> ControllerMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleSettingsAction;

	TObjectPtr<ACSStoreGameState> StoreGameState;
	ECStorePhase LastObservedStorePhase = ECStorePhase::Preparation;
	bool bClosingAnnouncementPending = false;
	bool bCraftCloseupActive = false;
};
