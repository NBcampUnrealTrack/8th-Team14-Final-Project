#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSTutorialTypes.h"
#include "CSTutorialSubsystem.generated.h"

class UDataTable;
class ACSStoreGameMode;
class ACSTrashCanActor;
class ACSPlayerCharacter;
class ACSStorePlayerController;
class UUserWidget;
class UCSTutorialMarkerComponent;
class UCSTutorialBannerWidgetBase;
class UCSTutorialDialogueWidgetBase;
class UCSTutorialObjectiveWidgetBase;

// 튜토리얼 스텝이 바뀔 때마다 방송
// 위젯들이 이걸 구독해서 화면 내용을 갱신함
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnTutorialStepChanged,
	const FCSTutorialStepRow&,
	NewStep);

// 목표 진행도(예: 쓰레기 1/3)가 바뀔 때마다 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnTutorialObjectiveProgress,
	int32,
	Current,
	int32,
	Goal);

// 튜토리얼이 완전히 끝났을 때(정상 완료든 cs.SkipTutorial이든) 딱 한 번 방송
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

	// 지금 진행 중인 스텝의 목표 진행도를 갱신
	// 목표 수치에 도달하면 자동으로 다음 스텝으로 넘어감
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void NotifyObjectiveProgress(ECSTutorialStepId StepId, int32 NewCurrent);

	// [디버그 전용] 완료 조건과 무관하게 강제로 다음 스텝으로 넘김
	// 콘솔 명령 cs.TNext가 이 함수를 호출
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Debug")
	void DebugForceAdvanceStep();

protected:
	void StartTutorial(ACSStoreGameMode* GameMode);
	void AdvanceStep();
	void HandlePartFinished(ECSTutorialPart FinishedPart);
	void HandlePart1Finished();
	void CompleteTutorial();
	void BroadcastCurrentStep() const;
	void StartAutoAdvanceTimerIfNeeded();
	void ClearAutoAdvanceTimer();

	// 현재 스텝의 HighlightMarkerTag를 가진 액터를 레벨에서 찾아 마커를 켬
	void UpdateHighlightMarkersForCurrentStep();

	// 현재 켜져 있는 마커를 전부 끄고 목록을 비움
	void HideAllActiveMarkers();

	// 배너/대화창/목표 위젯 3종을 생성해 화면에 올림
	void CreateTutorialWidgets(ACSStoreGameMode* GameMode);

	// 생성했던 위젯 3종을 화면에서 내리고 참조를 정리
	void CloseTutorialWidgets();

	// 레벨에 있는 쓰레기통 등, 튜토리얼을 모르는 상설 시설의 방송을 찾아서 구독
	void SubscribeToLevelDisposables();

	// 구독했던 상설 시설 방송을 전부 해제
	void UnsubscribeFromLevelDisposables();

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

	UFUNCTION()
	void HandleTrashDisposed(AActor* DisposedItem, int32 TotalDisposedCount);

	// PlayerController의 OnGameplayUIInputLockChanged를 구독해 호출됨
	// 지금 스텝이 기다리는 위젯이 맞는지 확인 후 열림/닫힘 조건에 맞으면 NotifyStepCompleted 호출
	UFUNCTION()
	void HandleGameplayUIInputLockChanged(UUserWidget* RequestingWidget, bool bLocked);

	UFUNCTION()
	void HandleBuildModeChanged(bool bIsBuildMode);

	UFUNCTION()
	void HandleBuildableFacilityPlaced(AActor* FacilityActor);

private:
	// DataTable에서 읽어온 행들을 테이블에 적힌 순서 그대로 담아둠
	// 이 배열의 인덱스가 곧 진행 순서
	TArray<FCSTutorialStepRow*> StepRows;

	int32 CurrentStepIndex = INDEX_NONE;
	int32 CleanTrashDisposedCount = 0;
	bool bTutorialCompleted = false;
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

	// 지연 구독으로 찾은 플레이어 컨트롤러 / 구독 해제 시 사용
	UPROPERTY()
	TObjectPtr<ACSStorePlayerController> SubscribedPlayerController;

	// 건축 모드 진입/시설 배치 이벤트의 RemoveDynamic을 위해 구독 대상을 보관
	UPROPERTY()
	TObjectPtr<ACSPlayerCharacter> SubscribedPlayerCharacter;

	// TrySubscribeToPlayerController()가 재시도에 사용하는 타이머 핸들
	FTimerHandle PlayerControllerSubscribeRetryTimerHandle;

	// 무한 재시도를 막기 위한 안전장치용 카운터
	int32 PlayerControllerSubscribeAttempts = 0;

	// 현재 스텝에서 켜둔 마커 컴포넌트 목록
	UPROPERTY()
	TArray<TObjectPtr<UCSTutorialMarkerComponent>> ActiveMarkerComponents;
};
