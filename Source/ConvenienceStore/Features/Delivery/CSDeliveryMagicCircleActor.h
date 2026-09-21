#pragma once

#include "CoreMinimal.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSDeliveryMagicCircleActor.generated.h"

class UBoxComponent;
class UArrowComponent;
class USceneComponent;
class UStaticMeshComponent;
class UCSDeliveryOrderSettingsDataAsset;
class UCSDeliverySettlementWidget;
class ACSProductPickupActor;
class ACSDeliveryCrateActor;
class UNiagaraComponent;
class UNiagaraSystem;
class ACSStoreGameState;

USTRUCT(BlueprintType)
struct FCSMagicCircleLevelVFX
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraSystem> IdleEffect;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraSystem> ReadyEffect;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraSystem> DispatchEffect;
};

/** 상점 내에서 선택한 원격 배달 주문을 발송하는 마법진입니다. */
UCLASS()
class CONVENIENCESTORE_API ACSDeliveryMagicCircleActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSDeliveryMagicCircleActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	/** 플레이어의 상품 배치 프리뷰와 실제 투하가 같은 지점을 사용합니다. */
	FTransform GetDeliveryPreviewTransform() const;
	/** 발송할 수 없는 상태일 때, 조준 HUD에 표시할 안내 문구입니다. */
	FText GetUnavailableInteractionText() const;
	bool IsDispatchReady() const;
	bool HasRequiredProducts() const;
	bool PlaceProduct(ACSProductPickupActor* Product);
	bool PlaceOneProductFromCrate(ACSDeliveryCrateActor* DeliveryCrate);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 놓인 상품을 검사할 마법진 범위입니다. 상품 콜리전은 Overlap 이벤트를 생성해야 합니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> DeliveryZone;

	/** 상품 감지와 별개로 플레이어 Visibility 추적만 받는 상호작용 범위입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> InteractionCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> DeliveryDropPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MagicCircleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> MagicCircleVFX;
	// 배열 0~4에 1~5단계의 효과를 지정합니다. 비어 있으면 해당 효과를 재생하지 않습니다.
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadOnly, Category = "Delivery Order|VFX")
	TArray<FCSMagicCircleLevelVFX> LevelVFX;
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Order|VFX")
	void OnMagicCircleVisualUpdated(int32 Level);
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Order|VFX")
	void OnDeliveryDispatched();
	// 지속 외형을 추가할 때도 같은 준비 상태를 사용합니다. HUD는 이 상태를 즉시 검사합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Delivery Order|VFX")
	void OnDeliveryReadyChanged(bool bReady);

	/** 상품 후보와 원격 수신처 목록을 담은 설정 에셋입니다. */
	// 모든 동일 마법진 인스턴스가 BP 기본값의 같은 주문 설정을 사용합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order")
	TObjectPtr<UCSDeliveryOrderSettingsDataAsset> OrderSettings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery Order|UI")
	TSubclassOf<UCSDeliverySettlementWidget> DeliverySettlementWidgetClass;

private:
	UFUNCTION() void HandleMagicCircleUpgraded(int32 Level);
	void UpdateLevelVFX(int32 Level);
	void GetAvailableProducts(TArray<ACSProductPickupActor*>& OutProducts) const;
	UFUNCTION() void RefreshReadyState();
	bool bProductsReady = false;
	FTimerHandle ReadyStateTimer;
	void PlayOneShotVFX(UNiagaraSystem* Effect);
	UPROPERTY(Transient) TObjectPtr<ACSStoreGameState> StoreState;
};
