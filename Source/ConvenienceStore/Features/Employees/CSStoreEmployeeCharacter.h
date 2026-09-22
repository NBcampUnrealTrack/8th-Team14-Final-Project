#pragma once

#include "CoreMinimal.h"
#include "Data/CSStoreEmployeeTypes.h"
#include "GameFramework/Character.h"
#include "Data/CSEmployeeHiringTypes.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Tools/CSHitReactable.h"
#include "CSStoreEmployeeCharacter.generated.h"

class UCSStoreEmployeeRoleDataAsset;
class USceneComponent;
class ACSDeliveryCrateActor;
class ACSProductPickupActor;
class ACSFreshnessSafeZoneActor;
class ACSCheckoutCounterActor;
class UCSEmployeeHiringWidget;
class UTextRenderComponent;
class UBoxComponent;

// 진열 업무를 재고 위치부터 진열 완료까지 세분화한 내부 작업 단계입니다.
enum class ECSStoreEmployeeRestockPhase : uint8
{
	None,
	MoveToSource,
	PickUpCrate,
	MoveToShelf,
	MoveToRecoveryStorage,
	MoveToReturnCrate,
	MoveToReturnProduct,
	TakeProductFromCrate,
	PlaceProductOnShelf,
};

// 매장 업무 하나를 수령해 이동하고 완료하는 공통 직원 캐릭터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSStoreEmployeeCharacter : public ACharacter, public ICSInteractable, public ICSHitReactable
{
	GENERATED_BODY()
	friend class FCSEmployeePackingRecoveryTest;

public:
	ACSStoreEmployeeCharacter();
	virtual void ApplyMagicHit_Implementation(FVector HitLocation, FVector ImpactDirection, float KnockbackStrength) override;
	UFUNCTION(BlueprintPure, Category = "Employee|Magic") float GetMagicBoostRemainingSeconds() const;
	UFUNCTION(BlueprintImplementableEvent, Category = "Employee|Magic") void OnMagicBoostChanged(bool bActive);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	UFUNCTION(BlueprintPure, Category = "Employee|Hiring") bool RequiresHiring() const { return bRequiresHiring; }
	UFUNCTION(BlueprintPure, Category = "Employee|Hiring") ECSEmployeeHireSlot GetHiringSlot() const { return HiringSlot; }
	UFUNCTION(BlueprintPure, Category = "Employee|Hiring") bool CanWork() const;
	FText GetHiringRoleName() const;
	// 고용소에서 지연 생성한 직원의 BeginPlay 전에 설정합니다.
	void ConfigureAsApplicant(ECSEmployeeHireSlot Slot, TSubclassOf<UCSEmployeeHiringWidget> WidgetClass = nullptr);
	void RefreshEmploymentPresentation();

	// 현재 역할이 처리할 수 있는 업무인지 서브시스템이 확인할 수 있도록 제공합니다.
	bool CanHandleTask(ECSStoreEmployeeTaskType TaskType) const;
	// 플레이어가 가져간 재고를 직원의 반환·정리 경로에서 다시 건드리지 않도록 소유 참조를 넘깁니다.
	void RelinquishInventoryToPlayer(AActor* Inventory);
	AActor* GetCarriedInventoryForPlayer() const;

	// 직원 관리 UI에서 담당 중인 업무를 읽을 수 있도록 제공합니다.
	UFUNCTION(BlueprintPure, Category = "Employee")
	bool HasActiveTask() const { return bHasActiveTask; }

	UFUNCTION(BlueprintPure, Category = "Employee")
	FCSStoreEmployeeTask GetActiveTask() const { return ActiveTask; }

	UFUNCTION(BlueprintPure, Category = "Employee|Idle")
	bool IsReturningToIdleLocation() const { return bHasIdleDestination && !bAtIdleDestination; }

	UFUNCTION(BlueprintPure, Category = "Employee|Idle")
	bool IsWaitingAtIdleLocation() const { return bAtIdleDestination; }

	// 애니메이션 블루프린트가 상자 운반 몽타주와 양손 IK를 전환할 수 있도록 현재 운반 상태를 제공합니다.
	UFUNCTION(BlueprintPure, Category = "Employee|Carry")
	bool IsCarryingCrate() const;

	UFUNCTION(BlueprintPure, Category = "Employee|Carry")
	bool IsHoldingProduct() const;

	// 다른 직원과만 이동 충돌을 무시합니다. 플레이어·손님·월드 충돌은 유지합니다.
	void SetIgnoreEmployeeMovement(ACSStoreEmployeeCharacter* OtherEmployee, bool bIgnore);

protected:
	// 운반 중인 재고는 이동 충돌 없이 플레이어의 E 조준만 받을 수 있게 합니다.
	UPROPERTY(VisibleAnywhere, Category = "Employee|Carry") TObjectPtr<UBoxComponent> InventoryInteractionBox;
	// 재피격 시 배율을 중첩하지 않고 남은 시간만 갱신합니다.
	UPROPERTY(EditAnywhere, Category = "Employee|Magic", meta = (ClampMin = "0.1")) float MagicBoostDuration = 10.0f;
	UPROPERTY(EditAnywhere, Category = "Employee|Magic", meta = (ClampMin = "1.0")) float MagicMoveMultiplier = 1.5f;
	UPROPERTY(EditAnywhere, Category = "Employee|Magic", meta = (ClampMin = "1.0")) float MagicWorkMultiplier = 1.5f;
	float MoveSpeedBeforeBoost = 0.0f;
	float WorkSpeedMultiplier = 1.0f;
	UPROPERTY(VisibleAnywhere, Category = "Employee|Magic") TObjectPtr<UTextRenderComponent> MagicBoostNameplate;
	FTimerHandle MagicBoostTimer;
	void EndMagicBoost();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Hiring")
	TSubclassOf<UCSEmployeeHiringWidget> HiringWidgetClass;
	// 기존 업무 전용 배치 직원은 그대로 동작합니다. 고용소가 생성하는 지원자는 true입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Hiring") bool bRequiresHiring = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Hiring", meta = (EditCondition = "bRequiresHiring"))
	ECSEmployeeHireSlot HiringSlot = ECSEmployeeHireSlot::FirstRestock;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Hiring") TObjectPtr<UTextRenderComponent> HiringNameplate;
	UPROPERTY(Transient) TObjectPtr<UCSEmployeeHiringWidget> ActiveHiringWidget;
	// 역할별 처리 가능 업무와 작업 시간을 지정하는 데이터 에셋입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Role")
	TObjectPtr<UCSStoreEmployeeRoleDataAsset> RoleData;

	// 별도 역할 에셋을 아직 만들지 않은 테스트용 직원이 처리할 업무 목록입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Role")
	TArray<ECSStoreEmployeeTaskType> FallbackSupportedTaskTypes;

	// 전용 직원 하위 클래스가 역할 데이터 에셋의 잘못된 설정으로 다른 업무를 받지 않도록 제한할지 여부입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Employee|Role")
	bool bLockToFallbackSupportedTaskTypes = false;

	// 계산대 대기 정책은 공용 구역으로 대체하지 않습니다. 계산 전담 역할에는 자동 적용됩니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Idle")
	bool bWaitAtCheckoutCounter = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Idle", meta = (ClampMin = "0.0", Units = "s"))
	float IdleReturnDelaySeconds = 4.0f;

	// 목적지 액터에 이 거리 이내로 도착하면 이동을 멈추고 작업을 시작합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float WorkAcceptanceRadius = 160.0f;

	// 경로 추종 컴포넌트의 실제 정지 지점과 코드의 거리 계산 오차를 흡수해, 도착 후 업무 단계로 안정적으로 전환합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WorkArrivalTolerance = 100.0f;

	// 작업 위치에 도착한 뒤 대상을 향해 몸을 돌리는 초당 각도입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float WorkFacingTurnRate = 360.0f;

	// NavMesh 투영 지점이 원본 상자와 멀리 떨어진 경우, 직원이 원격으로 집어 드는 일을 막는 실제 상자 집기 거리입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SourcePickupMaxDistance = 300.0f;

	// 낱개 상품은 바닥·선반 주변 NavMesh 투영 지점과 약간 벌어질 수 있으므로, 상자보다 여유 있는 집기 거리를 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float LooseProductPickupMaxDistance = 400.0f;

	// 단별 상품 변경으로 오배치가 된 상품은 진열대 앞 NavMesh가 상품보다 멀리 투영될 수 있습니다.
	// 이 업무에만 600cm까지의 넓은 집기 범위를 주어, 한 단의 남은 상품이 반복 업무로 남는 일을 막습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MisplacedShelfProductPickupMaxDistance = 600.0f;

	// 배송 포털의 직원 전용 접근 지점에 실제로 도착했을 때만 적용하는 상자 집기 거리입니다.
	// 일반 진열대·창고 상자는 SourcePickupMaxDistance를 계속 사용하므로 원격 집기 문제가 생기지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float DeliveryPortalPickupMaxDistance = 1200.0f;

	// 위의 배송 전용 예외를 적용할 수 있는 접근 지점과의 최대 수평 거리입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float DeliveryPortalApproachTolerance = 150.0f;

	// 손에 든 상품을 표시할 오른손 소켓 이름입니다. 리그에 맞는 소켓 이름으로 BP에서 조정하실 수 있습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Carry")
	FName RightHandSocketName = TEXT("hand_r");

	// 배송 상자를 든 뒤 진열대로 이동하기 전까지 보여 줄 집기 연출 시간입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Carry", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CratePickupDurationSeconds = 0.6f;

	// 상자에서 상품을 꺼내 오른손에 든 모습이 보이는 시간입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Carry", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ProductPickupDurationSeconds = 0.35f;

	// 오른손의 상품을 진열 슬롯에 놓기 전 유지하는 시간입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Carry", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ProductPlacementDurationSeconds = 0.35f;

	// 오른손 소켓이 없는 리그에서도 상품을 보이게 하기 위한 보조 앵커입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Carry")
	TObjectPtr<USceneComponent> RightHandCarryAnchor;

	// 배송 상자를 한 번만 부착하는 운반 앵커입니다. 양손은 애니메이션 또는 IK로 상자 손잡이에 맞추는 방식으로 사용합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Carry")
	TObjectPtr<USceneComponent> CrateCarryAnchor;

	// 상자 좌·우 손잡이에 대응하는 IK 목표점입니다. 애님 BP에서 양손 IK의 Effector 위치로 사용하시면 됩니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Carry")
	TObjectPtr<USceneComponent> CrateLeftHandIKTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Carry")
	TObjectPtr<USceneComponent> CrateRightHandIKTarget;

private:
	const TArray<ECSStoreEmployeeTaskType>& GetSupportedTaskTypes() const;
	void TryAcquireTask();
	void UpdateIdleBehavior(float DeltaSeconds);
	bool ShouldWaitAtCheckoutCounter() const;
	bool TryFindIdleDestination();
	bool TryProjectIdleLocation(const FVector& Location, FVector& OutNavigationLocation) const;
	bool GetIdleDestination(FVector& OutLocation, FRotator& OutFacing) const;
	void CancelIdleReturn(bool bReleaseCheckoutStation = true);
	void UpdateActiveTask(float DeltaSeconds);
	void UpdateRestockTask(float DeltaSeconds);
	void UpdateStorageTask(float DeltaSeconds);
	bool ExecuteActiveTask();
	void ClearActiveTask(bool bSucceeded);
	bool GetCurrentWorkLocation(FVector& OutWorkLocation, AActor*& OutTargetActor) const;
	bool TryGetCurrentWorkFacing(const AActor* TargetActor, FRotator& OutFacing) const;
	// 회전이 끝날 때까지 업무 타이머와 실제 상호작용을 진행하지 않습니다.
	bool UpdateWorkFacing(const AActor* TargetActor, float DeltaSeconds);
	bool UpdateFacingRotation(const FRotator& TargetFacing, float DeltaSeconds);
	void RestoreWorkRotationSettings();
	bool MoveToWorkLocation(const FVector& WorkLocation, AActor* TargetActor);
	void BeginRestockPhase(ECSStoreEmployeeRestockPhase NewPhase);
	// 실패·취소·진열대 포화 등 종료 사유와 무관하게, 손에 든 재고를 창고 우선으로 반납합니다.
	bool BeginHeldInventoryReturn(bool bTaskSuccessAfterReturn);
	bool BeginFailureStorageRecovery();
	bool FindFailureRecoveryStorage(ACSFreshnessSafeZoneActor*& OutStoragePoint) const;
	bool StoreHeldInventoryInRecoveryStorage();
	bool BeginHeldCrateReturn();
	bool BeginHeldProductReturn();
	bool FindSafeCrateReturnLocation(FVector& OutReturnLocation) const;
	bool PickUpSourceCrate();
	bool TryPackNearbyProducts(ACSProductPickupActor* SourceProduct);
	UPROPERTY(EditDefaultsOnly, Category = "Employee|Packing") TSubclassOf<ACSDeliveryCrateActor> PackingCrateClass;
	// 이동 없이 손이 닿는 주변 재고만 모아서 먼 상품을 원격으로 회수하지 않게 합니다.
	UPROPERTY(EditAnywhere, Category = "Employee|Packing", meta = (ClampMin = "1.0")) float PackingSearchRadius = 400.0f;

	bool TakeProductFromHeldCrate();
	bool PlaceHeldProductOnShelf();
	void ReleaseHeldCrate();
	void ReleaseHeldProductToReturnLocation();
	void ClearHeldProduct();

	FCSStoreEmployeeTask ActiveTask;
	bool bHasActiveTask = false;
	bool bHasReachedWorkTarget = false;
	bool bHasIssuedMoveRequest = false;
	bool bHasIdleDestination = false;
	bool bIdleMoveRequested = false;
	bool bAtIdleDestination = false;
	float IdleElapsedSeconds = 0.0f;
	float IdleSearchCountdown = 0.0f;
	float IdleMoveElapsedSeconds = 0.0f;
	int32 IdleSlotIndex = INDEX_NONE;
	FVector IdleMoveGoal = FVector::ZeroVector;
	FVector IdleDestinationAnchor = FVector::ZeroVector;
	FTransform IdleTargetTransform = FTransform::Identity;
	TWeakObjectPtr<AActor> IdleTarget;
	// 작업 중에만 이동/컨트롤러 회전을 끄고, 다음 이동이나 업무 종료 시 원래 BP 설정으로 복원합니다.
	bool bHasWorkRotationOverride = false;
	bool bSavedUseControllerRotationYaw = false;
	bool bSavedOrientRotationToMovement = false;
	bool bSavedUseControllerDesiredRotation = false;
	// 계산대가 자동 스캔 연출을 끝낼 때까지 계산 직원이 업무를 유지하도록 합니다.
	bool bWaitingForEmployeeCheckout = false;
	bool bHeldProductWasSpawnedFromCrate = false;
	bool bHasHeldCrateReturnLocation = false;
	bool bHasHeldProductReturnTransform = false;
	bool bHasAttemptedFailureStorageRecovery = false;
	// 진열대가 가득 차 남은 상자를 반환할 때에도, 업무 자체는 성공으로 완료해야 같은 상자 업무가 재생성되지 않습니다.
	bool bReturnHeldCrateAsTaskSuccess = false;
	float WorkElapsedSeconds = 0.0f;
	float MoveSettledElapsedSeconds = 0.0f;
	// 업무가 없을 때 매 프레임 로그가 쌓이지 않도록, 미수령 사유 출력 주기를 제한합니다.
	float NoTaskDiagnosticElapsedSeconds = 0.0f;
	int32 MoveRetryCount = 0;
	ECSStoreEmployeeRestockPhase RestockPhase = ECSStoreEmployeeRestockPhase::None;
	int32 RemainingRestockQuantity = 0;

	// 현재 몸통 앞에 들고 있는 실제 배송 상자입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSDeliveryCrateActor> HeldCrate;

	// 진열 수량이 남아 상자를 다시 내려놓을 때 직원 발밑이 아니라 원래 집었던 위치로 되돌리기 위한 좌표입니다.
	FVector HeldCrateReturnLocation = FVector::ZeroVector;
	FTransform HeldProductReturnTransform = FTransform::Identity;

	UPROPERTY(Transient)
	TObjectPtr<ACSFreshnessSafeZoneActor> FailureRecoveryStoragePoint;

	// 현재 오른손에 들고 있으며 다음 진열 슬롯에 놓을 상품입니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSProductPickupActor> HeldProduct;
};
