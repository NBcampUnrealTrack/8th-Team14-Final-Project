#pragma once

#include "CoreMinimal.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryOrderTerminalActor.generated.h"

class ACSDeliveryDestinationActor;
class ACSDeliveryOrderIndicatorActor;
class ACSStorePlayerController;
class UCSDeliveryOrderWidget;
class UCSDeliveryOrderSubsystem;
class UDataTable;
class USceneComponent;

// 배달 주문 하나를 수락하는 최소 단말기입니다. UI 주문 목록으로 확장하기 위한 진입점입니다.
UCLASS()
class CONVENIENCESTORE_API ACSDeliveryOrderTerminalActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSDeliveryOrderTerminalActor();
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	UDataTable* GetProductDataTable() const { return ProductDataTable; }

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	FCSDeliveryOrderRuntime GetActiveDeliveryOrder() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	TArray<FCSDeliveryOrderRuntime> GetDeliveryOrders() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	int32 GetTodayGeneratedOrderCount() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	int32 GetAcceptedOrderCount() const;

	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool CanAcceptActiveDeliveryOrder(FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool AcceptActiveDeliveryOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool DeclineActiveDeliveryOrder(FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool CancelActiveDeliveryOrder(FText& OutMessage);

	// 목록 UI의 각 주문 행이 자신의 주문 번호로 동작을 요청할 때 사용합니다.
	UFUNCTION(BlueprintPure, Category = "Delivery Order")
	bool CanAcceptDeliveryOrder(int32 OrderId, FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool AcceptDeliveryOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool DeclineDeliveryOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	bool CancelDeliveryOrder(int32 OrderId, FText& OutMessage);

	UFUNCTION(BlueprintCallable, Category = "Delivery Order")
	void CloseDeliveryOrderUI(UCSDeliveryOrderWidget* RequestingWidget);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 랜덤 주문 후보를 만들 때 읽을 상품 마스터 데이터 테이블입니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Delivery Order")
	TObjectPtr<UDataTable> ProductDataTable;

	// 주문을 확인하고 수락·거절하는 블루프린트 UI입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order|UI")
	TSubclassOf<UCSDeliveryOrderWidget> DeliveryOrderWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderWidget> ActiveDeliveryOrderWidget;

	// 수락 대기 주문이 있을 때 터미널 위에 표시할 공통 화살표입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order|Indicator")
	TSubclassOf<ACSDeliveryOrderIndicatorActor> DeliveryOrderIndicatorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order|Indicator")
	FVector DeliveryOrderIndicatorOffset = FVector(0.0f, 0.0f, 250.0f);

	UPROPERTY(Transient)
	TObjectPtr<ACSDeliveryOrderIndicatorActor> DeliveryOrderIndicator;

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderSubsystem> BoundDeliveryOrderSubsystem;

	UFUNCTION()
	void HandleDeliveryOrdersChanged();

	void RefreshDeliveryOrderIndicator();
};
