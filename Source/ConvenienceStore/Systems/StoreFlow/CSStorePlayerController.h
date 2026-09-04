#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "GameFramework/PlayerController.h"
#include "Data/CSStorePhaseTypes.h"
#include "CSStorePlayerController.generated.h"

class ACSStoreGameState;
class UCSStoreHUDWidget;
class UUserWidget;
class UCSDailySummaryWidget;
class UCSStatusAnnouncementWidget;
class UCSClosingWarningWidget;
class UTexture2D;
enum class ECSStatusAnnouncementType : uint8;

// 발주/계산/장부처럼 게임 조작을 막는 UI가 열리거나 닫힐 때마다 방송
// 튜토리얼이 어떤 UI가 열렸는지 감지하는 경로
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnGameplayUIInputLockChanged,
	UUserWidget*,
	RequestingWidget,
	bool,
	bLocked);

// 로컬 플레이어의 편의점 HUD와 마감 UI 입력 상태를 관리합니다.
UCLASS()
class CONVENIENCESTORE_API ACSStorePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 임시 타이틀의 시작 버튼에서 호출합니다. 정식 타이틀 도입 시 이 구역 전체를 제거할 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "UI|Temporary Title")
	void StartGameFromTemporaryTitle();

	// 발주·계산처럼 게임 조작을 막아야 하는 UI가 열리고 닫힐 때 입력 잠금을 갱신합니다.
	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetGameplayUIInputLock(UUserWidget* RequestingWidget, bool bShouldLock);

	// 계산·정산처럼 상시 HUD를 가려야 하는 UI의 요청을 등록하거나 해제합니다.
	// 요청이 모두 해제된 뒤에만 HUD를 이전 표시 상태로 복원합니다.
	UFUNCTION(BlueprintCallable, Category = "UI|HUD")
	void SetStoreHUDHidden(UObject* Requester, bool bShouldHide);

	// 계산·발주 등 게임 조작을 막는 UI가 현재 열려 있는지 반환합니다.
	bool IsGameplayUIInputLocked() const;

	// 진열할 빈 슬롯이 없을 때 HUD에 빨간 배치 불가 안내를 표시합니다.
	void ShowPlacementUnavailablePrompt();

	// 조준한 대상의 상호작용 안내를 HUD에 전달합니다.
	void SetInteractionPrompt(const FText& Message, const FLinearColor& TextColor);

	// 조준한 상품의 판매가를 HUD에 전달합니다.
	void SetProductPrice(const FText& Message);

	// 조준한 개별 상품의 남은 유통기한과 표시 색상을 HUD에 전달합니다.
	void SetProductShelfLife(const FText& Message, const FLinearColor& TextColor);

	// 냉장·냉동 상품의 현재 신선도와 표시 색상을 HUD에 전달합니다.
	void SetProductFreshness(const FText& Message, const FLinearColor& TextColor);

	// 진열 보관 환경이 맞지 않을 때 HUD에 경고를 전달합니다.
	void SetStorageWarning(const FText& Message);

	// 배치 대상이 바뀌거나 상품을 내려놓았을 때 HUD의 조준 안내를 지웁니다.
	void ClearInteractionContext();

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

protected:
	virtual void BeginPlay() override;
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

	void UpdateClosingInputMode();
	void ShowStatusAnnouncement(ECSStatusAnnouncementType AnnouncementType);
	void TryShowPendingClosingAnnouncement();
	bool HasGameplayUIInputLock() const;
	UUserWidget* GetFocusedGameplayUIWidget() const;

	// === Temporary Title Screen: 정식 타이틀 도입 시 이 구역과 BeginPlay 호출 한 줄 제거 ===
	void ShowTemporaryTitleScreen();
	void CloseTemporaryTitleScreen();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Temporary Title")
	bool bUseTemporaryTitleScreen = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Temporary Title")
	TSoftClassPtr<UUserWidget> TemporaryTitleWidgetClass =
		TSoftClassPtr<UUserWidget>(FSoftObjectPath(
			TEXT("/Game/ConvenienceStore/UI/Title/WBP_Title.WBP_Title_C")));

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ActiveTemporaryTitleWidget;
	// === End Temporary Title Screen ===

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UCSStoreHUDWidget> StoreHUDWidgetClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Closing")
	TSubclassOf<UCSDailySummaryWidget> DailySummaryWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UCSDailySummaryWidget> ActiveDailySummaryWidget;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Announcement")
	TSubclassOf<UCSStatusAnnouncementWidget> StatusAnnouncementWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSStatusAnnouncementWidget> ActiveStatusAnnouncementWidget;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Closing Warning")
	TSubclassOf<UCSClosingWarningWidget> ClosingWarningWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<UCSClosingWarningWidget> ActiveClosingWarningWidget;

	UPROPERTY()
	TObjectPtr<UCSStoreHUDWidget> StoreHUDWidget;

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

	TObjectPtr<ACSStoreGameState> StoreGameState;
	ECStorePhase LastObservedStorePhase = ECStorePhase::Preparation;
	bool bClosingAnnouncementPending = false;
};
