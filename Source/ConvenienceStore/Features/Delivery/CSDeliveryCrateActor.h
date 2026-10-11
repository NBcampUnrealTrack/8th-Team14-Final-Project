#pragma once

#include "CoreMinimal.h"
#include "Data/CSProductTypes.h"
#include "Engine/DataTable.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Trash/CSTrashable.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryCrateActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UBoxComponent;
class UTexture2D;
class UCSProductVisualRegistry;
class ACSProductPickupActor;
class UPrimitiveComponent;
class UNiagaraComponent;
class ACSStoreGameState;
class ACSStoreEmployeeCharacter;
struct FCSCrateRecord;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCSOnCrateQuantityChanged,
	int32,
	NewQuantity);

UCLASS()
class CONVENIENCESTORE_API ACSDeliveryCrateActor : public AActor,
	public ICSInteractable,
	public ICSTrashable
{
	GENERATED_BODY()

public:
	ACSDeliveryCrateActor();
	static constexpr int32 PackingCapacity = 6;
	// 포장 중에는 원본을 유지하며, 운반자가 상자를 확보한 뒤에만 원본을 제거합니다.
	bool InitializePackedProducts(const TArray<ACSProductPickupActor*>& Products);
	bool TryAddPackedProduct(ACSProductPickupActor* Product, AActor* Carrier, int32 TaskId = INDEX_NONE);
	static bool CanPackProduct(const ACSProductPickupActor* Product, const AActor* Carrier = nullptr, int32 TaskId = INDEX_NONE);
	static void RemovePackedSource(ACSProductPickupActor* Product);
	UFUNCTION(BlueprintPure, Category = "Delivery|Packing") bool IsPackingCrate() const { return bPackingCrate; }

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool InitializeCrate(FDataTableRowHandle InProductRow, int32 InQuantity);

	// 발주 배송으로 막 도착한 상자인지 표시합니다. 입고 대기 상자는 직원이 다른 진열 업무보다 먼저 창고로 옮깁니다.
	UFUNCTION(BlueprintCallable, Category = "Delivery|Intake")
	void SetAwaitingStockroomIntake(bool bInAwaitingStockroomIntake);

	// 입고 대기 여부를 블루프린트와 직원 업무 생성 로직에서 안전하게 확인할 수 있도록 제공합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery|Intake")
	bool IsAwaitingStockroomIntake() const
	{
		return bAwaitingStockroomIntake;
	}

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FName GetProductId() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FText GetProductDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	FDataTableRowHandle GetProductRow() const;

	UFUNCTION(BlueprintPure, Category = "Delivery|Product")
	FCSProductRuntimeState GetProductRuntimeState() const
	{
		return ProductRuntimeState;
	}

	// 세이브 복원 전용으로 상품 런타임 상태를 적용
	// InitializeCrate()는 신선도를 초기값으로만 설정하므로 저장 시점 상태를 되돌릴 때 사용
	UFUNCTION(BlueprintCallable, Category = "Delivery|Product")
	void ApplyProductRuntimeState(const FCSProductRuntimeState& InProductRuntimeState);

	// 상품·수량·신선도·포장 상태를 세이브 레코드에 기록
	// 포장 상자의 낱개별 상태가 protected이므로 상자가 직접 기록
	void CaptureSaveRecord(FCSCrateRecord& OutRecord) const;

	// 세이브 레코드의 신선도·입고 대기·포장 상태를 적용
	// InitializeCrate()와 FinishSpawning() 이후에 호출
	void RestoreSaveRecord(const FCSCrateRecord& InRecord);

	UFUNCTION(BlueprintPure, Category = "Delivery|Freshness")
	int32 GetCurrentFreshness() const;

	UFUNCTION(BlueprintPure, Category = "Delivery|Product")
	TSubclassOf<ACSProductPickupActor> GetProductActorClass() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	int32 GetRemainingQuantity() const;

	UFUNCTION(BlueprintPure, Category = "Delivery")
	TSoftObjectPtr<UTexture2D> GetProductIcon() const;

	UFUNCTION(BlueprintCallable, Category = "Delivery")
	bool ConsumeOne();

	// 직원 업무가 이 상자를 처리하는 동안 플레이어·다른 직원의 중복 집기를 막습니다.
	bool TryReserveForEmployee(ACSStoreEmployeeCharacter* Employee, int32 TaskId);
	bool CanBeReservedByEmployee(const ACSStoreEmployeeCharacter* Employee, int32 TaskId) const;
	void ReleaseEmployeeReservation(const ACSStoreEmployeeCharacter* Employee, int32 TaskId);
	bool IsReservedByEmployee() const;
	void ClearEmployeeReservationForPlayer() { ReservedByEmployee = nullptr; ReservationTaskId = INDEX_NONE; ReservationStartTimeSeconds = 0.0f; }

	ACSProductPickupActor* SpawnProductActor() const;

	// 물리를 중지하고 CrateMesh를 Blueprint 기본 상대 Transform으로 복원합니다.
	void PrepareForPickupPhysics();

	// 플레이어가 월드 바닥에 내려놓은 배송 상자의 물리를 활성화합니다.
	void EnableWorldDropPhysics();

	void EnterFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void ExitFreshnessSafeZone(ECSProductShelfType SafeStorageType);
	void SetHeldByPlayerForFreshness(bool bIsHeldByPlayer);

	FVector GetHeldRelativeLocation() const
	{
		return HeldRelativeLocation;
	}

	// 수레 적재 프리뷰가 실제 배송 상자와 같은 메시·상대 Transform을 복사할 수 있도록 제공합니다.
	const UStaticMeshComponent* GetCrateMeshComponent() const
	{
		return CrateMesh;
	}

	UPROPERTY(BlueprintAssignable, Category = "Delivery")
	FCSOnCrateQuantityChanged OnQuantityChanged;

	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery")
	void OnCrateDepleted();

protected:
	// 상품마다 유통기한과 신선도를 보존하여 포장으로 상품 상태가 초기화되지 않게 합니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery|Packing") bool bPackingCrate = false;
	UPROPERTY(Transient) TArray<FCSProductRuntimeState> PackedProductStates;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CrateMesh;

	// 물리 상자 대신 제압된 손님만 막는 비시뮬레이션 충돌 상자
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SubduedCollisionProxy;

	// 현재 상자 메시의 로컬 경계에 충돌 상자를 맞춰 설정
	void UpdateSubduedCollisionProxy();

	// 발주 시스템과 무관하게 레벨에 직접 배치한 상자의 초기 상품과 수량을 지정합니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Delivery|Placed Initial",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle PlacedProductRow;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Delivery|Placed Initial",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 PlacedQuantity = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery")
	int32 RemainingQuantity = 0;

	// 레벨에 직접 둔 상자를 입고 상자처럼 시험할 수 있고, 실제 발주 상자는 배송 포털이 런타임에 true로 설정합니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Delivery|Intake")
	bool bAwaitingStockroomIntake = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery|Product")
	FCSProductRuntimeState ProductRuntimeState;

	UPROPERTY(Transient)
	int32 MatchingFreshnessSafeZoneCount = 0;

	UPROPERTY(Transient)
	bool bIsHeldByPlayerForFreshness = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery|Employee")
	TObjectPtr<ACSStoreEmployeeCharacter> ReservedByEmployee = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery|Employee")
	int32 ReservationTaskId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Delivery|Employee")
	float ReservationStartTimeSeconds = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SpoiledVFXComponent;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> SpoiledVFXStoreGameState;

	FTimerHandle SpoiledVFXRefreshTimerHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Holding")
	FVector HeldRelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Physics",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PawnPushImpulseStrength = 50.0f;

	UFUNCTION()
	void HandlePawnOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSpoiledVFXBusinessDayStarted(int32 NewBusinessDay);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Product")
	TSubclassOf<ACSProductPickupActor> ProductActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery|Visual")
	TObjectPtr<UCSProductVisualRegistry> VisualRegistry;

	void EnsureFreshnessRuntimeStateInitialized();
	void UpdateFreshnessDecayState();
	void CacheSpoiledVFXComponent();
	void RefreshSpoiledVFXTimer();
	void HandleSpoiledVFXRefreshTimer();
	void UpdateSpoiledVFX();
};
