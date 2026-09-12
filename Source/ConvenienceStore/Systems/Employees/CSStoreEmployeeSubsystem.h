#pragma once

#include "CoreMinimal.h"
#include "Data/CSStoreEmployeeTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSStoreEmployeeSubsystem.generated.h"

class ACSStoreEmployeeCharacter;
class ACSShelfActor;
class ACSCheckoutCounterActor;

// 계산 대기열과 빈 진열대를 감시하여, 직원이 처리할 업무를 생성·배정하는 월드 단위 관리자입니다.
UCLASS()
class CONVENIENCESTORE_API UCSStoreEmployeeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// 레벨에 배치된 직원이 BeginPlay에서 자신을 등록합니다.
	void RegisterEmployee(ACSStoreEmployeeCharacter* Employee);
	// 직원이 제거될 때 배정 중이던 업무를 다시 대기 상태로 되돌립니다.
	void UnregisterEmployee(ACSStoreEmployeeCharacter* Employee);

	// 역할이 처리 가능한 업무 하나를 원자적으로 가져옵니다.
	bool TryClaimBestTask(
		ACSStoreEmployeeCharacter* Employee,
		const TArray<ECSStoreEmployeeTaskType>& SupportedTaskTypes,
		FCSStoreEmployeeTask& OutTask,
		FString* OutNoClaimReason = nullptr);
	// 플레이어가 아직 직원 손에 들리지 않은 예약 재고를 집을 때 해당 업무를 취소합니다.
	bool CancelTaskForPlayerPickup(AActor* SourceActor);
	// 직원이 외부 취소된 업무를 계속 수행하지 않도록 현재 배정 상태를 확인합니다.
	bool IsTaskAssignedToEmployee(int32 TaskId, const ACSStoreEmployeeCharacter* Employee) const;
	// 직원이 업무를 끝냈거나 실패했을 때 상태와 참조를 정리합니다.
	void FinishTask(int32 TaskId, ACSStoreEmployeeCharacter* Employee, bool bSucceeded);
	// AI 디버그 또는 직원 관리 UI에서 현재 업무 목록을 읽을 수 있도록 제공합니다.
	const TArray<FCSStoreEmployeeTask>& GetTasks() const { return Tasks; }
	// 플레이어가 단별 상품 설정을 바꾼 직후처럼, 다음 주기를 기다리지 않고 업무 목록을 갱신해야 할 때 호출합니다.
	void RefreshTasksImmediately();

	// 대기와 실제 계산 업무가 동일한 계산대 자리를 공유합니다. 계산대당 직원은 한 명만 배정합니다.
	bool CanUseCheckoutStation(const ACSStoreEmployeeCharacter* Employee, const ACSCheckoutCounterActor* Counter) const;
	bool TryReserveCheckoutStation(ACSStoreEmployeeCharacter* Employee, ACSCheckoutCounterActor* Counter);
	ACSCheckoutCounterActor* GetReservedCheckoutStation(const ACSStoreEmployeeCharacter* Employee) const;
	void ReleaseCheckoutStation(const ACSStoreEmployeeCharacter* Employee);

private:
	// 같은 재고/대상에 대해 도달 실패가 반복될 때, 즉시 재수령하는 왕복을 막기 위한 상태입니다.
	struct FTaskRetryState
	{
		int32 ConsecutiveFailureCount = 0;
		double DeferredUntilSeconds = 0.0;
	};

	// 일정 주기로 실제 월드 상태를 다시 읽어 필요한 업무만 생성합니다.
	void RefreshTasks();
	void RefreshCheckoutTasks();
	void RefreshReorganizationTasks();
	void RefreshInboundIntakeTasks();
	void RefreshPerishableProtectionTasks();
	void RefreshRestockTasks();
	void RefreshStorageTasks();
	bool HasActiveTask(ECSStoreEmployeeTaskType Type, const AActor* TargetActor) const;
	bool HasActiveTaskForSource(const AActor* SourceActor) const;
	bool HasPendingReorganizationFromShelf(const ACSShelfActor* Shelf) const;
	AActor* GetTaskRetryKeyActor(const FCSStoreEmployeeTask& Task) const;
	bool IsTaskDeferred(const FCSStoreEmployeeTask& Task) const;
	void RegisterTaskFailure(const FCSStoreEmployeeTask& Task);
	void ResetTaskFailure(const FCSStoreEmployeeTask& Task);

	UPROPERTY(Transient)
	TArray<FCSStoreEmployeeTask> Tasks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACSStoreEmployeeCharacter>> RegisteredEmployees;

	// 업무가 새로 생성되어도 같은 원본 상자/상품을 곧바로 다시 선택하지 않도록 유지합니다.
	TMap<TWeakObjectPtr<AActor>, FTaskRetryState> TaskRetryStates;
	TMap<TWeakObjectPtr<ACSCheckoutCounterActor>, TWeakObjectPtr<ACSStoreEmployeeCharacter>> CheckoutStations;

	float RefreshElapsedSeconds = 0.0f;
	int32 NextTaskId = 1;

	// 빈 진열대를 너무 늦게 발견하지 않으면서도 매 프레임 액터를 순회하지 않도록 제한하는 주기입니다.
	UPROPERTY(EditAnywhere, Category = "Employee|Tasks", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float RefreshIntervalSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Employee|Tasks", meta = (ClampMin = "1", UIMin = "1"))
	int32 FailureCountBeforeCooldown = 3;

	UPROPERTY(EditAnywhere, Category = "Employee|Tasks", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float RetryDelaySeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Employee|Tasks", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float FailureCooldownSeconds = 15.0f;
};
