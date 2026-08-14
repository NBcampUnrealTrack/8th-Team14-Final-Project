#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSTutorialTypes.h"
#include "CSTutorialSubsystem.generated.h"

class UDataTable;

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

	// 지금 진행 중인 스텝의 목표 진행도를 갱신 (예: 쓰레기 2개째 주움)
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void NotifyObjectiveProgress(ECSTutorialStepId StepId, int32 NewCurrent);

	// [디버그 전용] 완료 조건과 무관하게 강제로 다음 스텝으로 넘김
	// 콘솔 명령 cs.TNext가 이 함수를 호출
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Debug")
	void DebugForceAdvanceStep();

protected:
	void StartTutorial(UDataTable* InStepDataTable);
	void AdvanceStep();
	void HandlePartFinished(ECSTutorialPart FinishedPart);
	void HandlePart1Finished();
	void CompleteTutorial();
	void BroadcastCurrentStep() const;

private:
	// DataTable에서 읽어온 행들을 테이블에 적힌 순서 그대로 담아둠
	// 이 배열의 인덱스가 곧 진행 순서
	TArray<FCSTutorialStepRow*> StepRows;

	int32 CurrentStepIndex = INDEX_NONE;
	bool bTutorialCompleted = false;
};
