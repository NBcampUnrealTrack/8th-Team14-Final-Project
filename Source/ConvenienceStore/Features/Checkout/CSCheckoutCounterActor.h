#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSCheckoutTypes.h"
#include "Data/CSStorePhaseTypes.h"
#include "Features/Customers/Interface/CSCustomerCounterInterface.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSCheckoutCounterActor.generated.h"

class APlayerController;
class APawn;
class UArrowComponent;
class UBoxComponent;
class UCameraComponent;
class UDataTable;
class USceneComponent;
class UStaticMeshComponent;
class UCSCheckoutWidget;
class ACSStoreGameState;
class ACSProductPickupActor;
class ACSCustomerCharacter;

// 손님 대기열의 위치를 제공하는 기본 계산대 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSCheckoutCounterActor
	: public AActor
	, public ICSCustomerCounterInterface
	, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSCheckoutCounterActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// 대기열 순서에 맞는 손님의 월드 좌표를 반환
	virtual FVector GetQueueSlotLocation(const int32 Index) const override;

	// 계산대가 지정한 트랜스폼에 놓였다고 가정했을 때의 Index번째 슬롯 좌표
	// 건축 배치 미리보기처럼 액터를 실제로 옮기지 않고 판정만 해야 하는 곳에서 사용
	FVector GetQueueSlotLocationForTransform(const int32 Index,const FTransform& CounterTransform) const;

	// Index번째 슬롯의 도착 판정 영역을 NavMesh 높이에 맞춰 반환
	virtual bool TryGetQueueSlotZone(const int32 Index, FVector& OutCenter, float& OutRadius) const override;

	// 주어진 월드 좌표가 Index번째 슬롯 영역 안인지 확인
	// "줄 순번상 맨 앞"과 "지금 계산 가능"을 가르는 판정
	virtual bool IsPointInsideQueueSlotZone(const int32 Index, const FVector& WorldPoint) const override;

	// Index번째 슬롯에 선 손님이 바라볼 방향
	virtual bool TryGetQueueSlotFacing(const int32 Index, FRotator& OutFacing) const override;

	// 손님이 슬롯으로 이동할 때 멈춰도 되는 반경
	// QueueSlotStopRadius를 QueueSlotSpacing·QueueSlotRadius가 정하는 상한으로 잘라서 반환
	virtual float GetQueueSlotStopRadius() const override;

	// 데이터가 요구하는 정원과 실제로 설 자리가 있는 슬롯 수 중 작은 쪽
	virtual int32 GetEffectiveMaxQueueLength() const override;

	// 나중에 아이템·업그레이드·기획이 대기줄 정원을 늘리거나 줄일 때 호출할 자리
	UFUNCTION(BlueprintCallable, Category = "Queue")
	void AddBonusQueueSlots(int32 Delta);

	// 계산대가 지정한 트랜스폼에 있다고 가정하고, 대기줄 슬롯이 전부 설 수 있는 자리인지 확인
	bool AreQueueSlotsUsableAt(const FTransform& CounterTransform) const;

	// 플레이어가 계산대와 상호작용했을 때 맨 앞 손님의 계산 완료를 처리합니다.
	virtual void Interact_Implementation(AActor* Interactor) override;

	// 현재 계산대의 사용 가능 상태에 맞는 상호작용 안내 문구를 반환합니다.
	virtual FText GetInteractionText_Implementation() const override;

	// 플레이어의 직원 구역 진입 여부를 반영한 계산대 안내 문구를 반환합니다.
	FText GetInteractionTextForPlayer(const AActor* PlayerActor) const;

	// 지정한 액터가 계산을 진행할 수 있는 직원 구역에 있는지 확인합니다.
	UFUNCTION(BlueprintPure, Category = "Checkout")
	bool IsInCashierInteractionZone(const AActor* Actor) const;

	// 대기열 맨 앞 손님의 구매 목록을 상품별 수량과 금액이 정리된 주문으로 변환합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout")
	bool BuildFrontCustomerOrder(
		TArray<FCSCheckoutLineItem>& OutLineItems,
		int32& OutTotalSalePrice) const;

	// 직원 시스템이 현재 계산대를 처리 대상으로 삼아도 되는지 확인합니다.
	UFUNCTION(BlueprintPure, Category = "Checkout|Employee")
	bool CanEmployeeProcessCheckout() const;

	// 계산 직원이 UI를 열지 않고, 계산대 상품 연출을 시작합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout|Employee")
	bool TryProcessEmployeeCheckout();
	bool IsEmployeeCheckoutInProgress() const { return bIsEmployeeCheckoutPresenting; }
	bool DidLastEmployeeCheckoutSucceed() const { return bLastEmployeeCheckoutSucceeded; }

protected:
	// 현재 매장 단계에서 계산 처리가 허용되는지 확인합니다.
	bool CanProcessCheckout() const;

	// 손님의 주문을 계산 UI에 전달하고, 플레이어의 입력을 UI로 전환합니다.
	bool OpenCheckoutWidget(AActor* Interactor);

	// 스캔 완료 후 위젯이 요청한 실제 결제 완료 처리를 수행합니다.
	UFUNCTION()
	void HandleCheckoutPaymentRequested();

	// 결제 취소 요청을 받으면 거래와 대기열을 유지한 채 계산 UI만 닫습니다.
	UFUNCTION()
	void HandleCheckoutCancelled();

	// 상품 한 개를 스캔하면 계산대 위에 표시한 동일 상품을 제거합니다.
	UFUNCTION()
	void HandleCheckoutItemScanned(FName ProductId);

	// 현재 영수증 퇴장이 끝나면 다음 손님 주문을 같은 UI와 카메라에 연결합니다.
	UFUNCTION()
	void HandleReceiptExitFinished();

	// 대기열의 다음 손님 주문과 월드 표시 상품을 현재 계산 세션에 준비합니다.
	bool PrepareNextCustomerCheckout();

	// 다음 손님이 0번 자리에 도착할 때까지 카메라와 UI를 유지하며 대기
	void BeginWaitForNextCustomer();
	void TickWaitForNextCustomer();

	// 대기 상태를 해제하고 폴링 타이머를 정리
	void EndWaitForNextCustomer();

	// 계산대가 응대 중인 손님의 인내심을 멈추기 위해 상태 갱신
	void SetCustomerBeingServed(ACSCustomerCharacter* Customer, bool bIsServed);

	// 대기줄 영역 박스를 QueueStartPoint / QueueSlotSpacing / MaxQueueSlots 로부터 다시 계산
	void SyncQueueZoneBox();

	// QueueStartPoint와 손님이 서는 바닥의 높이 차이를 상대 좌표 길이로 반환
	// 영역 박스 밑면을 바닥에 맞추는 데만 사용
	float GetQueueZoneFloorDropLocal(const float ParentScaleZ) const;

	// NavMesh를 판정 근거로 믿을 수 있는 상태인지 확인
	bool IsQueueNavigationTrustworthy() const;

	// 슬롯 XY 자리의 바닥 높이를 찾습니다.
	bool TryProjectQueueSlotToFloor(const FVector& SlotLocation, float& OutFloorZ) const;

	// 대기줄 정원이 되어야 할 데이터상의 값, NavMesh 상한을 적용하기 전 값
	int32 GetConfiguredQueueSlotCount() const;

	// 슬롯 좌표가 NavMesh에 올라오고 다른 시설 몸통에 막히지 않은 개수를 세어 물리적 상한을 구함
	// 0번부터 순서대로 검사하고 처음 실패한 지점에서 멈춤
	int32 CountUsableQueueSlots(const int32 SearchLimit, const FTransform& CounterTransform) const;

	// 슬롯 자리가 다른 건축 시설 몸통에 막혀 있는지 확인
	bool IsQueueSlotBlockedByFacility(const FVector& SlotCenter) const;

	// 매장 단계가 계산 불가 상태로 바뀌면 진행 중인 계산 UI를 안전하게 닫습니다.
	UFUNCTION()
	void HandleStorePhaseChanged(ECStorePhase NewPhase);

	// 계산 UI를 닫고 플레이어 입력을 게임 조작으로 되돌립니다.
	void CloseCheckoutWidget(bool bRestorePawnImmediately = false);

	// 계산 시작 전 시점을 저장하고 계산대에 배치한 카메라로 전환합니다.
	void ActivateCheckoutCamera(APlayerController* PlayerController);

	// 계산 시작 전에 저장한 플레이어 시점으로 되돌립니다.
	void RestoreCheckoutCamera(bool bRestorePawnImmediately = false);

	// 계산 시작 전에 저장한 Hidden 상태로 플레이어 Pawn을 복원합니다.
	void RestoreCheckoutPlayerVisibility(bool bImmediate);
	void HandleCheckoutPlayerVisibilityRestore();

	// 대기열 맨 앞 손님의 상품 BP를 계산대 상판에 표시용으로 생성합니다.
	void SpawnCheckoutDisplayProducts();

	// 계산 종료, 취소, 단계 전환 시 표시용으로 생성한 상품을 모두 제거합니다.
	void ClearCheckoutDisplayProducts();
	// 계산 중인 고객의 장바구니 표시를 숨기거나 복원합니다.
	void SetCheckoutCustomerBasketVisible(bool bShouldBeVisible);
	bool BeginEmployeeCheckoutPresentation();
	void UpdateEmployeeCheckoutPresentation(float DeltaSeconds);
	bool CompleteEmployeeCheckoutPresentation();
	void CancelEmployeeCheckoutPresentation();
	bool IsCustomerOrderCheckoutEligible(
		const ACSCustomerCharacter* Customer,
		const ACSStoreGameState* CurrentStoreGameState) const;

	// 계산대 메시와 대기열 안내 요소를 부착하기 위한 루트 컴포넌트입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 블루프린트에서 계산대 메시를 지정할 수 있는 컴포넌트입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CounterMesh;

	// 계산 중 계산대와 손님을 정면으로 보여 주는 카메라입니다. 위치와 회전은 계산대 BP에서 조정합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkout|Camera")
	TObjectPtr<UCameraComponent> CheckoutCamera;

	// 손님 대기열 반대편의 직원 위치에서만 계산을 시작하도록 제한하는 충돌 영역입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CashierInteractionZone;

	// 첫 번째 손님이 설 위치와 대기열이 뻗어갈 방향을 지정하는 편집용 화살표입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Queue")
	TObjectPtr<UArrowComponent> QueueStartPoint;

	// 계산 시작 시 손님 상품을 올려둘 기준 위치와 방향을 편집하는 화살표입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkout|Display")
	TObjectPtr<UArrowComponent> CheckoutDisplayOrigin;

	// 건축 모드에서 대기줄 자리를 보호하는 영역, 판정 시 태그만 쓰이고 충돌은 꺼 둠
	// 진열대의 BrowseZoneBox와 같은 방식이며, 크기는 QueueStartPoint에서 파생됩니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Queue")
	TObjectPtr<UBoxComponent> QueueZoneBox;

	// 대기열에서 손님과 손님 사이에 유지할 거리
	// 겹침을 막는 장치는 이 값 하나뿐입니다. 제일 큰 손님(오크) 지름 150이 하한입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue",
		meta = (ClampMin = "150.0", UIMin = "150.0"))
	float QueueSlotSpacing = 280.0f;

	// 각 대기열 슬롯의 도착 판정 반경
	// MoveToLocation 허용 반경(30)보다 커야 손님이 멈춘 자리가 영역 안이고,
	// QueueSlotSpacing의 절반보다 작아야 인접 슬롯 영역이 겹치지 않습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue",
		meta = (ClampMin = "31.0", UIMin = "31.0"))
	float QueueSlotRadius = 80.0f;

	// 손님이 "내 자리에 섰다"고 보고 멈춰도 되는 반경
	// 도착 판정 반경(QueueSlotRadius)보다 좁아야 앞뒤 손님이 서로에게 치우쳐 멈춰도 몸이 겹치지 않습니다.
	// 여기 적은 값이 그대로 쓰이지는 않고 GetQueueSlotStopRadius가 간격 기준 상한으로 자릅니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue",
		meta = (ClampMin = "10.0", UIMin = "10.0"))
	float QueueSlotStopRadius = 30.0f;

	// 이 계산대에 기본으로 설 수 있는 사람 수
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue", meta = (ClampMin = "1"))
	int32 MaxQueueSlots = 6;

	// 나중에 아이템·업그레이드가 더해 줄 추가 정원
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Queue")
	int32 BonusQueueSlots = 0;

	// 대기줄 영역의 반폭입니다. 제일 큰 손님 반지름(75)보다 커야 몸이 영역 안에 들어옴
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue",
		meta = (ClampMin = "75.0", UIMin = "75.0"))
	float QueueZoneHalfWidth = 80.0f;

	// 대기줄 영역의 높이입니다. 진열대 BrowseZoneHeight와 같은 값으로 맞춥니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float QueueZoneHeight = 200.0f;

	// 0번 손님만 예외적으로 계산대 정면을 보게 할지 여부
	// 켜면 계산 카메라에 손님의 정면이 잡히고, 끄면 그림처럼 전원이 전진 방향을 봅니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue")
	bool bFrontCustomerFacesCounter = true;

	// 손님의 상품 RowName으로 이름과 가격을 조회할 상품 마스터 데이터 테이블입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Checkout")
	TObjectPtr<UDataTable> ProductMasterDataTable;

	// 계산대 상호작용 시 생성할 계산 UI 블루프린트 클래스입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Checkout")
	TSubclassOf<UCSCheckoutWidget> CheckoutWidgetClass;

	// 플레이어 시점과 계산대 시점 사이를 전환하는 시간입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkout|Camera",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CheckoutCameraBlendTime = 0.35f;

	// 계산대 위에 한 줄로 표시할 최대 상품 수입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkout|Display",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 CheckoutDisplayProductsPerRow = 4;

	// 계산대 위 상품 사이의 좌우 간격입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkout|Display",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CheckoutDisplayRightSpacing = 35.0f;

	// 계산대 위 상품 줄 사이의 앞뒤 간격입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkout|Display",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CheckoutDisplayForwardSpacing = 45.0f;

	// 직원 자동 계산에서 다음 상품을 스캔하기까지의 간격입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkout|Employee",
		meta = (ClampMin = "0.1", UIMin = "0.1"))
	float EmployeeScanIntervalSeconds = 0.6f;

	// 다음 손님 도착을 확인하는 폴링 간격
	// 액터 Tick을 늘리지 않도록 기존 CheckoutPlayerVisibilityRestoreTimer와 같은 타이머 방식 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkout",
		meta = (ClampMin = "0.05", UIMin = "0.05"))
	float NextCustomerPollInterval = 0.2f;

	// 이 시간 안에 다음 손님이 도착하지 않으면 계산 세션을 닫습니다.
	// 경로가 막혀 손님이 영원히 못 오는 경우에 플레이어가 갇히지 않게 하는 안전장치
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkout",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NextCustomerWaitTimeout = 8.0f;

	// 현재 화면에 열려 있는 계산 UI를 저장합니다.
	UPROPERTY(Transient)
	TObjectPtr<UCSCheckoutWidget> ActiveCheckoutWidget;

	// 현재 계산 중인 손님을 위해 생성한 표시 전용 상품 액터입니다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACSProductPickupActor>> CheckoutDisplayProducts;

	// 표시용 액터와 같은 인덱스로 저장한 상품 ID입니다.
	UPROPERTY(Transient)
	TArray<FName> CheckoutDisplayProductIds;

	// 계산 UI가 열렸을 때의 고객을 보관하여 취소 시 장바구니를 다시 표시합니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSCustomerCharacter> ActiveCheckoutCustomer;

	// UI가 예외적으로 먼저 사라져도 원래 시점을 복원할 수 있도록 계산 중인 컨트롤러를 저장합니다.
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> CheckoutPlayerController;

	// 계산 시작 직전 플레이어가 바라보던 ViewTarget입니다.
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> PreviousCheckoutViewTarget;

	// 계산대 카메라에 몸이 가리지 않도록 계산 중에만 숨길 플레이어 Pawn입니다.
	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> CheckoutPlayerPawn;

	// 계산 시작 전 Pawn의 Hidden 상태를 종료 시 정확히 복원하기 위해 저장합니다.
	bool bCheckoutPlayerPawnWasHidden = false;
	bool bCheckoutPlayerVisibilityOverridden = false;
	FTimerHandle CheckoutPlayerVisibilityRestoreTimer;

	bool bCheckoutCameraActive = false;

	// 결제가 확정된 경우에는 장바구니를 복원하지 않고 봉투 표시를 유지합니다.
	bool bCheckoutPaymentCompleted = false;

	// 결제 확정부터 영수증 퇴장 완료까지 중복 결제와 취소 요청을 차단합니다.
	bool bIsCheckoutTransitioning = false;

	// 영수증 퇴장이 끝났지만 다음 손님이 아직 0번 자리로 걸어오는 중인 상태
	bool bIsWaitingForNextCustomer = false;
	FTimerHandle NextCustomerPollTimer;
	float NextCustomerWaitElapsed = 0.0f;

	// 직원 계산은 플레이어 UI와 달리 Tick 기반의 자동 스캔 연출로 진행됩니다.
	bool bIsEmployeeCheckoutPresenting = false;
	bool bLastEmployeeCheckoutSucceeded = false;
	float EmployeeScanElapsedSeconds = 0.0f;
	TArray<FCSCheckoutLineItem> ActiveEmployeeCheckoutOrder;

	// 영업 단계 변경을 감지하기 위해 현재 매장 GameState를 저장합니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> StoreGameState;
};
