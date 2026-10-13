#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSTutorialTypes.h"
#include "Data/CSStorePhaseTypes.h"
#include "Data/CSCraftTypes.h"
#include "CSTutorialSubsystem.generated.h"

class UDataTable;
class ACSStoreGameMode;
class ACSStoreGameState;
class ACSTrashCanActor;
class ACSShelfActor;
class ACSPlayerCharacter;
class ACSCustomerCharacter;
class ACSStorePlayerController;
class UUserWidget;
class UCSTutorialMarkerComponent;
class UCSTutorialBannerWidgetBase;
class UCSTutorialDialogueWidgetBase;
class UCSTutorialObjectiveWidgetBase;
class UCSOrderSubsystem;
class UCSCustomerRuntimeSubsystem;
class UCSDailySummaryWidget;
class UCSCraftingSubsystem;
class UCSTutorialChoiceWidget;
class ACSProductPickupActor;
class ACSDeliveryCrateActor;
class UInputComponent;
class UCSDeliveryOrderSubsystem;
struct FCSOrderResult;
struct FCSDeliveryOrderRuntime;

// 튜토리얼 스텝이 바뀔 때마다 방송
// 위젯들이 이걸 구독해서 화면 내용을 갱신함
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnTutorialStepChanged,
	const FCSTutorialStepRow&,
	NewStep);

// 목표 진행도가 바뀔 때마다 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnTutorialObjectiveProgress,
	int32,
	Current,
	int32,
	Goal);

// 튜토리얼이 완전히 끝났을 때 딱 한 번 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnTutorialCompleted);

// 튜토리얼 진행 상태를 소유하는 서브시스템
// 맵마다 하나씩 자동으로 생성되며, PIE를 새로 시작할 때마다 초기화됨
UCLASS()
class CONVENIENCESTORE_API UCSTutorialSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// 스텝이 바뀔 때마다 방송
	// 위젯이 구독해서 문구를 갱신
	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FCSOnTutorialStepChanged OnTutorialStepChanged;

	// 목표 진행도가 바뀔 때마다 방송
	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FCSOnTutorialObjectiveProgress OnTutorialObjectiveProgress;

	// 튜토리얼이 끝났을 때 방송
	// 위젯 정리 등에 사용
	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FCSOnTutorialCompleted OnTutorialCompleted;

	// 지금 진행 중인 스텝이 CompletedStepId와 일치하면 다음 스텝으로 넘김
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void NotifyStepCompleted(ECSTutorialStepId CompletedStepId);

	// 트리거 볼륨이 부른다. 제조 튜토리얼 중의 제조 건물 입구는 완료 대신 플레이어를 안으로 되돌린다
	void NotifyTriggerEntered(ECSTutorialStepId StepId, const AActor* Trigger);

	// 지금 진행 중인 스텝의 목표 진행도를 갱신
	// 목표 수치에 도달하면 자동으로 다음 스텝으로 넘어감
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void NotifyObjectiveProgress(ECSTutorialStepId StepId, int32 NewCurrent);

	// 완료 조건과 무관하게 강제로 다음 스텝으로 넘김
	// 콘솔 명령 cs.TNext와 플레이어 건너뛰기(N 키, 확인 창)가 이 함수를 호출
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Debug")
	void DebugForceAdvanceStep();

	// [디버그 전용] 배너/대화창/목표/마커(외곽선)까지 튜토리얼을 통째로 끔
	// 콘솔 명령 cs.TSkip이 이 함수를 호출
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Debug")
	void DebugSkipTutorial();

	// 발주창(WBP_Orderv2)을 지금 닫아도 되는지 확인. 조건 미달이면 경고 배너를 띄우고 false 반환
	// WBP_Orderv2 닫기 버튼에서 false 시 닫기를 막도록 Branch로 연결해야 함
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	bool CanCloseOrderWidget();

	// 3부 진행 중이고 아직 잠자기 스텝이 아니면 true와 침대에 띄울 문구
	bool ShouldBlockSleep(FText& OutMessage) const;

	// 1부 진행 중이고 아직 입간판 스텝이 아니면 true와 입간판에 띄울 문구
	bool ShouldBlockStoreOpen(FText& OutMessage) const;

	// 시작했고 아직 끝나지 않았으면 true. 시작 전이거나 완료·건너뛰기 뒤면 false
	bool IsTutorialInProgress() const;

	// 창고 스텝에서 보낸 상자면 true와 진열대에 띄울 문구
	bool ShouldBlockCrateShelving(const ACSDeliveryCrateActor* Crate, FText& OutMessage) const;

	// 마감 튜토리얼이 진입 대기 중이거나 진행 중인지 확인
	// 튜토리얼을 완료했거나 건너뛰었으면 false를 반환
	bool IsPart3Pending() const;

	// 1일차 마감 세이브를 불러온 뒤 3부 첫 스텝부터 튜토리얼을 시작
	// 이미 완료했거나 튜토리얼 준비에 실패하면 시작하지 않음
	void ResumePart3FromSave(ACSStoreGameMode* GameMode);

	// 배달 튜토리얼 동안 손님이 이 상품을 한 개도 못 사게 막는다. 진열대 상품이 줄면 안내 외곽선이 사라진다
	bool IsDeliveryTutorialProduct(FName ProductId) const { return bDeliveryTutorialActive && ProductId == DeliveryProductId; }

	// 제조 튜토리얼 중 솥 상태에 맞는 스텝. 실패하거나 솥에서 나오면 앞 스텝으로 돌아간다
	static ECSTutorialStepId ResolveCraftStep(
		bool bBrewDone, bool bPotReady, bool bCloseup, ECSCraftStep CraftStep, bool bHeatCorrect, bool bStirDone);

protected:
	// 임시 타이틀 화면이 게임을 멈춰둔 동안 튜토리얼이 먼저 시작되는 문제 방지
	// IsPaused()가 풀릴 때까지 짧은 간격으로 재시도
	void TryStartTutorialAfterTitleScreen();

	// 3부(마감 후) 첫 스텝 시작. 영업종료 안내가 끝난 뒤 HandleStoreClosedAnnouncementFinished가 호출함
	void EnterPart3();

	void StartTutorial(ACSStoreGameMode* GameMode);

	// 진행 상태 초기화, DataTable 스텝 수집, 위젯 생성, 이벤트 구독을 처리
	// DataTable이 없거나 스텝 행이 없으면 false를 반환
	bool PrepareTutorial(ACSStoreGameMode* GameMode);

	void AdvanceStep();
	// 스텝 시작 처리만 한다. 이전 스텝의 끝 처리는 부르는 쪽에서
	void EnterStep(int32 StepIndex);
	int32 FindStepIndex(ECSTutorialStepId StepId) const;
	void HandlePartFinished(ECSTutorialPart FinishedPart);
	void HandlePart1Finished();
	void CompleteTutorial();
	void BroadcastCurrentStep() const;
	// 진행 스텝 방송은 여기로 모은다. 건너뛸 수 있는 스텝이면 건너뛰기 힌트를 붙인다
	void BroadcastStepRow(const FCSTutorialStepRow& Row) const;
	void StartAutoAdvanceTimerIfNeeded();
	void ClearAutoAdvanceTimer();

	// 현재 스텝의 HighlightMarkerTag를 가진 액터를 레벨에서 찾아 마커를 켬
	void UpdateHighlightMarkersForCurrentStep();

	// Tag를 가진 액터를 레벨에서 찾아 마커를 켬 (UpdateHighlightMarkersForCurrentStep과 ShowOneShotTip이 공유)
	void ShowMarkersForTag(FName Tag, const FText& Label);

	// 지금 스텝이 DeliveryArrived이고 아직 스폰 안 했으면 매장 밖 스폰 지점에 튜토리얼 전용 선반을 하나 만듦
	void TrySpawnTutorialShelfDelivery();

	// 지금 스텝이 StockShelves이고 아직 안 만들었으면 스폰 지점들에 튜토리얼 전용 배송 상자를 만듦
	void TrySpawnTutorialCrateDeliveries();

	// 현재 켜져 있는 마커를 전부 끄고 목록을 비움
	void HideAllActiveMarkers();

	// 쓰레기 스텝이 끝날 때 레벨에 남은 쓰레기를 전부 정리. 목표 수보다 많이 깔려 있다
	void DestroyRemainingTrash();

	// [디버그 전용] PlaceFacility 스텝을 cs.TNext로 건너뛸 때 튜토리얼 선반의 강조 표시를 끔
	void DebugHideTutorialShelfHighlight();

	// [디버그 전용] StockShelves 스텝을 cs.TNext로 건너뛸 때 튜토리얼 배송 상자의 강조 표시를 끔
	void DebugHideTutorialCrateHighlight();

	// 배너/대화창/목표 위젯 3종을 생성해 화면에 올림
	void CreateTutorialWidgets(ACSStoreGameMode* GameMode);

	// 생성했던 위젯 3종을 화면에서 내리고 참조를 정리
	void CloseTutorialWidgets();

	// 레벨에 있는 쓰레기통 등, 튜토리얼을 모르는 상설 시설의 방송을 찾아서 구독
	void SubscribeToLevelDisposables();

	// 구독했던 상설 시설 방송을 전부 해제
	void UnsubscribeFromLevelDisposables();

	// 레벨에 있는 진열대를 찾아 상품 배치 방송을 구독
	void SubscribeToLevelShelves();

	// 구독했던 진열대 방송을 전부 해제
	void UnsubscribeFromLevelShelves();

	// 튜토리얼이 켜진 맵에서 아직 스폰되지 않았을 수 있는 ACSStorePlayerController를 찾아 구독을 시도
	// 찾지 못하면 짧은 텀을 두고 다시 시도
	void TrySubscribeToPlayerController();

	// 컨트롤러를 실제로 찾았을 때 호출
	void SubscribeToPlayerController(ACSStorePlayerController* PlayerController);

	// SubscribeToPlayerController에서 걸었던 구독을 해제
	void UnsubscribeFromPlayerController();

	// PlayerCharacter가 방송하는 범용 건축 성공 이벤트를 구독/해제
	void SubscribeToPlayerCharacter(ACSPlayerCharacter* PlayerCharacter);
	void UnsubscribeFromPlayerCharacter();

	// 발주 서브시스템의 상품 발주 접수 성공 방송을 구독/해제
	void SubscribeToOrderSubsystem();
	void UnsubscribeFromOrderSubsystem();

	// 게임 상태의 실제 개점 방송을 구독/해제
	void SubscribeToStoreGameState();
	void UnsubscribeFromStoreGameState();

	// 손님 런타임 서브시스템의 속마음 위젯 표시 방송을 구독/해제 (속마음 팁 트리거용)
	void SubscribeToCustomerRuntimeSubsystem();
	void UnsubscribeFromCustomerRuntimeSubsystem();

	// StepRows에서 Part가 TargetPart인 첫 행의 인덱스를 찾음. 없으면 INDEX_NONE
	int32 FindFirstStepIndexForPart(ECSTutorialPart TargetPart) const;

	// 2부 원포인트 팁(TipRows) 중 TipId에 해당하는 행을 찾아 한 번만 표시
	// 배너/대화창/목표 위젯은 기존 OnTutorialStepChanged 구독을 그대로 재사용
	void ShowOneShotTip(ECSTutorialStepId TipId);
	void DisplayTip(const FCSTutorialStepRow& TipRow);
	const FCSTutorialStepRow* FindTipRow(ECSTutorialStepId TipId) const;

	// 배달 튜토리얼
	void SubscribeToDeliverySubsystem();
	void UnsubscribeFromDeliverySubsystem();
	void EnterDeliveryStep(ECSTutorialStepId StepId);
	void FinishDeliveryTutorial(bool bOrderEndedEarly);
	void CheckDeliveryPickUp();
	void AttachDeliveryProductMarkers();
	// N 건너뛰기. 수락 단계는 대신 수락하고, 집기 단계는 배달 장소로, 배달 장소 단계는 튜토리얼을 끝낸다
	void SkipDeliveryStep();
	AActor* FindDeliveryPlaceActor() const;

	UFUNCTION()
	void HandleDeliveryOrderChanged(const FCSDeliveryOrderRuntime& Order);

	// TipId 행의 ExpectedWidgetClass와 Widget의 클래스가 일치하는지 확인
	bool DoesTipExpectWidget(ECSTutorialStepId TipId, const UUserWidget* Widget) const;

	// 지금 떠 있는 팁(대화창/목표/마커)을 예약·표시 여부와 무관하게 즉시 정리
	// OpenSign 완료로 1부가 끝날 때, 그리고 팁 대상과 상호작용해 팁을 넘길 때 둘 다 사용
	void ClearTutorialDisplay();

	// 다이어리/게시판 위젯의 열림·닫힘을 감지해 팁 체인을 진행
	// 다이어리가 열리면 팁을 취소하고 숨기고, 닫히면 게시판 팁으로 넘어감
	// 게시판이 열리면(상호작용하면) 팁을 바로 숨김
	void HandleTipWidgetLockChanged(UUserWidget* RequestingWidget, bool bLocked);

	// 실시간 정산창(C, UCSDailySummaryWidget)의 열림·닫힘을 감지해 튜토리얼 화면을 잠깐 숨겼다가 그대로 되살림
	// 정산창은 스텝·팁 진행과 무관하게 아무 때나 열 수 있어 위 두 핸들러와 별도로 둠
	void HandleDailySummaryWidgetLockChanged(UUserWidget* RequestingWidget, bool bLocked);

	// StepRows[StepIndex]의 ExpectedWidgetClass와 Widget의 클래스가 일치하는지 확인
	// (DoesTipExpectWidget과 같은 패턴이나 대상이 TipRows가 아니라 StepRows)
	bool DoesStepExpectWidget(int32 StepIndex, const UUserWidget* Widget) const;

	// 수정구슬 발주창(WBP_Orderv2)의 열림·닫힘을 감지해 발주 흐름 전체를 담당
	// (PlaceProductOrder: 발주 없이 닫으면 경고 / FacilityOrderInfo: 시설탭 안 보고 닫으면 경고)
	// 발주/시설탭 두 스텝 다 같은 창을 계속 열어둔 채 이어지는 흐름이라 위 범용 위젯 완료 판정과 분리함
	void HandleOrderWidgetLockChanged(UUserWidget* RequestingWidget, bool bLocked);

	UFUNCTION()
	void HandleTrashDisposed(AActor* DisposedItem, int32 TotalDisposedCount);

	UFUNCTION()
	void HandleShelfItemPlaced(AActor* PlacedItem, int32 SlotIndex);

	// PlayerController의 OnGameplayUIInputLockChanged를 구독해 호출됨
	// 지금 스텝이 기다리는 위젯이 맞는지 확인 후 열림/닫힘 조건에 맞으면 NotifyStepCompleted 호출
	UFUNCTION()
	void HandleGameplayUIInputLockChanged(UUserWidget* RequestingWidget, bool bLocked);

	UFUNCTION()
	void HandleBuildModeChanged(bool bIsBuildMode);

	UFUNCTION()
	void HandleBuildableFacilityPlaced(AActor* FacilityActor);

	// OrderSubsystem의 OnOrderPlaced를 구독해 호출됨
	// 지금 스텝이 PlaceProductOrder면 스텝 완료 처리
	UFUNCTION()
	void HandleOrderPlaced(const FCSOrderResult& Result);

	// StoreGameState의 OnStoreOpenStateChanged를 구독해 호출됨
	// bNewStoreOpen이 true일 때(실제 개점)만 반응
	UFUNCTION()
	void HandleStoreOpenStateChanged(bool bNewStoreOpen);

	// PlayerController의 OnStoreOpenedAnnouncementFinished를 구독해 호출됨
	// "영업을 시작하였습니다" 안내 애니메이션이 끝난 직후 다이어리 팁을 띄움
	UFUNCTION()
	void HandleStoreOpenedAnnouncementFinished();

	// StoreGameState의 OnStorePhaseChanged를 구독해 호출됨
	// ClosingFreeRoam으로 바뀌면 3부 진입을 "예약"만 해둠 (실제 진입은 아래 함수가 맡음)
	UFUNCTION()
	void HandleStorePhaseChanged(ECStorePhase NewPhase);

	// PlayerController의 OnStoreClosedAnnouncementFinished를 구독해 호출됨
	// "영업시간이 종료되었습니다" 안내 애니메이션이 끝난 직후 3부(수정구슬 발주 등) 스텝을 시작
	// (HandleStoreOpenedAnnouncementFinished와 같은 이유로 고정 5초 타이머 대신 이 이벤트를 씀)
	UFUNCTION()
	void HandleStoreClosedAnnouncementFinished();

	// CustomerRuntimeSubsystem의 속마음 위젯 표시 방송을 구독해 호출됨 (속마음 팁 트리거)
	UFUNCTION()
	void HandleInnerThoughtShown();

	// CustomerRuntimeSubsystem의 머리 위 아이콘 표시 방송을 구독해 호출됨
	// 마법봉으로 봐야 하는 대화창보다 훨씬 자주·먼저 뜨므로 속마음 팁의 주 트리거로 씀
	UFUNCTION()
	void HandleHeadIconShown(ACSCustomerCharacter* Customer);

	// 살 수 있는 상품이 하나도 없으면 가장 싼 상품 한 상자 값까지 채워 준다. 채웠으면 true
	bool GrantTutorialOrderFundsIfNeeded();
	// 창을 열 때 지운 안내를 다시 띄운다. 처음 본 스텝 배너는 빼고
	void ShowCurrentStepWithoutBanner();

	// 제조 튜토리얼
	static bool IsCraftBundleStep(ECSTutorialStepId StepId);
	// 솥 상태를 따라 앞뒤로 옮겨 다니는 스텝
	static bool IsCraftBrewPhaseStep(ECSTutorialStepId StepId);
	void CheckCraftTutorialState();
	// 제조대 조작을 막을 이유. 빈 문구면 허용
	FText CheckCraftInput(ECSCraftInput Input) const;
	void HandleStepStarted();
	void HandleStepEnded();
	void SubscribeToCraftingSubsystem();
	void UnsubscribeFromCraftingSubsystem();
	void OpenChoiceWidget(const FCSTutorialStepRow& Row);
	void CloseChoiceWidget();
	TArray<ACSProductPickupActor*> PlaceTutorialIngredients(const TArray<FName>& ProductIds);
	void AttachCraftMarker(AActor* Target, const FText& Label);
	void RemoveCraftMarkers();
	// 여러 번 불러도 안전하다. 튜토리얼 물건과 마커를 지우고 선택 창을 닫는다
	void CleanUpCraftTutorial();

	// 창고 스텝. 배송 장소로 상자를 하나 더 보내고 창고에 넣었는지 주기적으로 본다
	void BeginStoreCrateStep();
	void CheckStoreCrateStored();
	// 튜토리얼 날 진열대가 비어 가면 품절 팁을 띄운다
	void CheckRestockTip();

	UFUNCTION()
	void HandleTutorialChoiceMade(bool bYes);

	// 플레이어가 N으로 지금 스텝을 건너뛴다. 확인 창에서 예를 골라야 넘어간다
	bool CanPlayerSkipCurrentStep() const;
	void HandleSkipKeyPressed();
	void CloseSkipConfirmWidget();

	UFUNCTION()
	void HandleSkipConfirmChoice(bool bYes);

	UFUNCTION()
	void HandleCraftIngredientAdded(int32 IngredientCount);

	UFUNCTION()
	void HandleManualCraftFinished(ECSCraftOutcome Outcome);

private:
	// DataTable에서 읽어온 행들을 테이블에 적힌 순서 그대로 담아둠 (Part가 PreOpen/PostClose인 행만)
	// 이 배열의 인덱스가 곧 진행 순서
	TArray<FCSTutorialStepRow*> StepRows;

	// Part가 DuringOpen(2부 원포인트 팁)인 행만 따로 담아둠. StepRows처럼 순서로 진행하지 않고
	// ShowOneShotTip이 StepId로 찾아 트리거 시점에 개별 표시
	TArray<FCSTutorialStepRow*> TipRows;

	// ShowOneShotTip으로 이미 표시한 팁. 같은 팁이 두 번 뜨는 것을 막음
	TSet<ECSTutorialStepId> ShownTipIds;


	// 1부가 끝나 2부 팁 체인이 진행 중인 동안만 true. 3부 시작·튜토리얼 종료 시 false로 돌아감
	// Tab 등 팁과 무관한 시점의 위젯 열림/닫힘이 팁 체인을 잘못 건드리지 않게 막는 용도
	bool bTipChainActive = false;

	// ClosingFreeRoam 전환 시 EnterPart3()를 이미 예약했는지. 중복 예약 방지용
	bool bPart3EntryScheduled = false;

	// 발주창(WBP_Orderv2)이 지금 열려 있는지. 열림 신호가 최초 열림인지 시설탭 재신호인지 구분하는 데 씀
	bool bOrderWidgetCurrentlyOpen = false;

	// 지금 발주창이 열려 있는 동안 실제로 발주를 1건 이상 했는지. 창을 닫을 때 이 값으로 경고 여부를 판단
	// 열릴 때마다 초기화됨
	bool bOrderWidgetOrderedThisOpen = false;

	// 지금 발주창이 열려 있는 동안 시설탭 재신호(HandleOrderWidgetLockChanged가 감지)를 받았는지
	// 열릴 때마다 초기화됨
	bool bOrderWidgetFacilityTabViewedThisOpen = false;

	int32 CurrentStepIndex = INDEX_NONE;
	int32 CleanTrashDisposedCount = 0;
	int32 StockShelvesPlacedCount = 0;
	bool bTutorialCompleted = false;
	bool bTutorialShelfSpawned = false;
	bool bTutorialCratesSpawned = false;

	// TrySpawnTutorialShelfDelivery가 스폰한 선반을 기억해둠
	// cs.TNext로 PlaceFacility를 건너뛸 때 실제 배치 이벤트 없이도 아웃라인을 끄기 위해 필요
	UPROPERTY()
	TArray<TObjectPtr<AActor>> TutorialSpawnedShelves;

	// TrySpawnTutorialCrateDeliveries가 스폰한 배송 상자를 기억해둠 (위 선반과 같은 이유)
	UPROPERTY()
	TArray<TObjectPtr<AActor>> TutorialSpawnedCrates;

	TWeakObjectPtr<ACSDeliveryCrateActor> StoreCrateTarget;
	FTimerHandle StoreCrateCheckTimerHandle;
	FTimerHandle RestockTipCheckTimerHandle;

	int32 PlacedFacilityCount = 0;

	FTimerHandle AutoAdvanceTimerHandle;

	UPROPERTY()
	TObjectPtr<UCSTutorialBannerWidgetBase> BannerWidgetInstance;

	UPROPERTY()
	TObjectPtr<UCSTutorialDialogueWidgetBase> DialogueWidgetInstance;

	UPROPERTY()
	TObjectPtr<UCSTutorialObjectiveWidgetBase> ObjectiveWidgetInstance;

	// Deinitialize/CompleteTutorial에서 RemoveDynamic으로 해제하기 위해 구독 목록을 기억해 둠
	UPROPERTY()
	TArray<TObjectPtr<ACSTrashCanActor>> SubscribedTrashCans;

	// 위와 같은 이유로 구독한 진열대 목록을 기억해 둠
	UPROPERTY()
	TArray<TObjectPtr<ACSShelfActor>> SubscribedShelves;

	// 지연 구독으로 찾은 플레이어 컨트롤러 / 구독 해제 시 사용
	UPROPERTY()
	TObjectPtr<ACSStorePlayerController> SubscribedPlayerController;

	// 건축 모드 진입/시설 배치 이벤트의 RemoveDynamic을 위해 구독 대상을 보관
	UPROPERTY()
	TObjectPtr<ACSPlayerCharacter> SubscribedPlayerCharacter;

	// 발주 서브시스템 구독 해제를 위해 보관
	UPROPERTY()
	TObjectPtr<UCSOrderSubsystem> SubscribedOrderSubsystem;

	UPROPERTY()
	TObjectPtr<ACSStoreGameState> SubscribedStoreGameState;

	// 속마음 팁 구독 해제를 위해 보관
	UPROPERTY()
	TObjectPtr<UCSCustomerRuntimeSubsystem> SubscribedCustomerRuntimeSubsystem;

	// TrySubscribeToPlayerController()가 재시도에 사용하는 타이머 핸들
	FTimerHandle PlayerControllerSubscribeRetryTimerHandle;

	// 무한 재시도를 막기 위한 안전장치용 카운터
	int32 PlayerControllerSubscribeAttempts = 0;

	// TryStartTutorialAfterTitleScreen()가 재시도에 사용하는 타이머 핸들
	FTimerHandle TitleScreenWaitTimerHandle;

	// 무한 재시도를 막기 위한 안전장치용 카운터
	int32 TitleScreenWaitAttempts = 0;

	// 현재 스텝에서 켜둔 마커 컴포넌트 목록
	UPROPERTY()
	TArray<TObjectPtr<UCSTutorialMarkerComponent>> ActiveMarkerComponents;

	// 정산창(C)처럼 튜토리얼과 무관한 위젯이 뜰 때 화면을 숨겼다가 그대로 되살리기 위해
	// BroadcastCurrentStep(진행 중인 스텝)이 마지막으로 방송한 행과 그 시점의 목표 진행도를 기억해둠.
	// 팁(ShowOneShotTip)은 일부러 갱신하지 않음 - BP 자체 타이머로 알아서 사라지는 1회성 알림이라
	// 여기서 되살리면 이미 사라진 지 오래인 팁이 다시 뜨는 문제가 생김. BroadcastCurrentStep이 const라 mutable
	mutable const FCSTutorialStepRow* LastBroadcastRow = nullptr;
	mutable int32 LastBroadcastObjectiveCurrent = 0;

	// 정산창 때문에 화면을 숨긴 상태인지. 정산창이 닫힐 때만 되살리도록 막는 가드
	bool bDisplaySuspendedForDailySummary = false;

	UPROPERTY()
	TObjectPtr<UCSCraftingSubsystem> SubscribedCraftingSubsystem;

	UPROPERTY()
	TObjectPtr<UCSTutorialChoiceWidget> ChoiceWidgetInstance;

	// 제조 튜토리얼 중에 동적으로 붙인 마커. 끝나면 떼어 낸다
	UPROPERTY()
	TArray<TObjectPtr<UCSTutorialMarkerComponent>> CraftMarkerComponents;

	// 아니요를 고른 경우에는 솥을 비우지 않는다. 플레이어가 넣어 둔 재료까지 지우면 안 된다
	bool bCraftTutorialAccepted = false;

	// 아니요를 고르면 다음 진행에서 제조 묶음 스텝을 시작 처리 없이 건너뛴다
	bool bSkippingCraftBundle = false;

	// 재료 넣기 스텝으로 돌아올 때 재료를 또 놓지 않게 한다
	bool bCraftIngredientsPlaced = false;
	bool bCraftBrewSucceeded = false;
	FTimerHandle CraftStateCheckTimerHandle;

	// 컨트롤러 코드를 고치지 않으려고 입력 컴포넌트를 컨트롤러 입력 스택에 얹는다
	UPROPERTY()
	TObjectPtr<UInputComponent> SkipInputComponent;

	UPROPERTY()
	TObjectPtr<UCSTutorialChoiceWidget> SkipConfirmWidgetInstance;

	// 확인 창을 띄운 스텝. 창이 떠 있는 동안 스텝이 바뀌면 예를 골라도 넘기지 않는다
	int32 SkipConfirmStepIndex = INDEX_NONE;
	// 배달 단계는 일반 스텝 번호가 안 바뀌어서 단계 이름으로 따로 확인한다
	bool bSkipConfirmForDelivery = false;
	ECSTutorialStepId SkipConfirmDeliveryStepId = ECSTutorialStepId::DeliveryAccept;

	// 입구에서 되돌릴 때마다 배너가 쌓이지 않게 마지막으로 띄운 시각을 기억한다
	double LastCraftHouseWarningTime = -1.0e9;

	UPROPERTY()
	TObjectPtr<UCSDeliveryOrderSubsystem> SubscribedDeliverySubsystem;

	bool bDeliveryTutorialActive = false;
	bool bDeliveryTutorialDone = false;
	ECSTutorialStepId DeliveryStepId = ECSTutorialStepId::DeliveryAccept;
	int32 DeliveryTutorialOrderId = INDEX_NONE;
	FName DeliveryProductId;
	FText DeliveryProductName;
	FTimerHandle DeliveryCheckTimerHandle;
	// 다 팔렸다는 배너를 한 번만 띄우고, 재고가 다시 생기면 마커를 새로 붙인다
	bool bDeliveryProductSoldOut = false;

	// 상품 이름을 채운 배달 안내 행. 정산창을 닫을 때 되살릴 수 있게 멤버로 둔다
	FCSTutorialStepRow DeliveryShownRow;

	// 다이어리·게시판·정산창 팁 중 지금 해야 하는 것. 배달 중에는 화면에 안 띄우고 기억만 했다가 끝나면 띄운다
	bool bHasPendingChainTip = false;
	ECSTutorialStepId PendingChainTipId = ECSTutorialStepId::TipDiary;

	// 배달 중에 차례가 된 배너 팁. 끝나면 이어서 띄운다
	TArray<ECSTutorialStepId> DeferredBannerTipIds;
};
