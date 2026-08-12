#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSCheckoutTypes.h"
#include "Data/CSStorePhaseTypes.h"
#include "Features/Customers/Interface/CSCustomerCounterInterface.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSCheckoutCounterActor.generated.h"

class UArrowComponent;
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
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 대기열 순서에 맞는 손님의 월드 좌표를 반환합니다.
	virtual FVector GetQueueSlotLocation(const int32 Index) const override;

	// 플레이어가 계산대와 상호작용했을 때 맨 앞 손님의 계산 완료를 처리합니다.
	virtual void Interact_Implementation(AActor* Interactor) override;

	// 현재 계산대의 사용 가능 상태에 맞는 상호작용 안내 문구를 반환합니다.
	virtual FText GetInteractionText_Implementation() const override;

	// 대기열 맨 앞 손님의 구매 목록을 상품별 수량과 금액이 정리된 주문으로 변환합니다.
	UFUNCTION(BlueprintCallable, Category = "Checkout")
	bool BuildFrontCustomerOrder(
		TArray<FCSCheckoutLineItem>& OutLineItems,
		int32& OutTotalSalePrice) const;

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

	// 매장 단계가 계산 불가 상태로 바뀌면 진행 중인 계산 UI를 안전하게 닫습니다.
	UFUNCTION()
	void HandleStorePhaseChanged(ECStorePhase NewPhase);

	// 계산 UI를 닫고 플레이어 입력을 게임 조작으로 되돌립니다.
	void CloseCheckoutWidget();

	// 대기열 맨 앞 손님의 상품 BP를 계산대 상판에 표시용으로 생성합니다.
	void SpawnCheckoutDisplayProducts();

	// 계산 종료, 취소, 단계 전환 시 표시용으로 생성한 상품을 모두 제거합니다.
	void ClearCheckoutDisplayProducts();
	// 계산 중인 고객의 장바구니 표시를 숨기거나 복원합니다.
	void SetCheckoutCustomerBasketVisible(bool bShouldBeVisible);

	// 계산대 메시와 대기열 안내 요소를 부착하기 위한 루트 컴포넌트입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 블루프린트에서 계산대 메시를 지정할 수 있는 컴포넌트입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CounterMesh;

	// 첫 번째 손님이 설 위치와 대기열이 뻗어갈 방향을 지정하는 편집용 화살표입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Queue")
	TObjectPtr<UArrowComponent> QueueStartPoint;

	// 계산 시작 시 손님 상품을 올려둘 기준 위치와 방향을 편집하는 화살표입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkout|Display")
	TObjectPtr<UArrowComponent> CheckoutDisplayOrigin;

	// 대기열에서 손님과 손님 사이에 유지할 거리입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Queue", meta = (ClampMin = "0.0"))
	float QueueSlotSpacing = 120.0f;

	// 손님의 상품 RowName으로 이름과 가격을 조회할 상품 마스터 데이터 테이블입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Checkout")
	TObjectPtr<UDataTable> ProductMasterDataTable;

	// 계산대 상호작용 시 생성할 계산 UI 블루프린트 클래스입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Checkout")
	TSubclassOf<UCSCheckoutWidget> CheckoutWidgetClass;

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

	// 결제가 확정된 경우에는 장바구니를 복원하지 않고 봉투 표시를 유지합니다.
	bool bCheckoutPaymentCompleted = false;

	// 영업 단계 변경을 감지하기 위해 현재 매장 GameState를 저장합니다.
	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> StoreGameState;
};
