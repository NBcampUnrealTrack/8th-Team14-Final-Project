#pragma once

#include "CoreMinimal.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Features/Items/CSProductPickupActor.h"
#include "CSProductPlacementPreviewActor.generated.h"

class UMaterialInterface;

// 진열대 배치 위치에서 상품 외형만 반투명하게 보여 주는 전용 프리뷰 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSProductPlacementPreviewActor : public ACSProductPickupActor
{
	GENERATED_BODY()

public:
	ACSProductPlacementPreviewActor();

	// 실제 상품의 DataAsset 설정과 ProductRow를 복사한 뒤 프리뷰 머티리얼을 설정합니다.
	bool InitializePreviewFromProduct(const ACSProductPickupActor* SourceProduct);

	// 아직 생성되지 않은 배송 상품의 클래스 기본 설정과 ProductRow로 프리뷰를 초기화합니다.
	bool InitializePreviewFromProductClass(
		FDataTableRowHandle InProductRow,
		TSubclassOf<ACSProductPickupActor> InProductActorClass);

	// 배치 가능 여부에 맞춰 프리뷰의 색상 머티리얼을 변경합니다.
	void SetPlacementValid(bool bIsPlacementValid);

	// 정상·경고·불가 상태에 맞춰 프리뷰 머티리얼을 변경합니다.
	void SetPlacementPreviewState(ECSShelfPlacementPreviewState NewState);

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

protected:
	// 배치 가능한 위치에서 사용할 반투명 초록색 머티리얼입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placement Preview")
	TObjectPtr<UMaterialInterface> ValidPlacementMaterial;

	// 배치 불가능한 위치에서 사용할 반투명 빨간색 머티리얼입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placement Preview")
	TObjectPtr<UMaterialInterface> InvalidPlacementMaterial;

	// 보관 환경이 맞지 않지만 배치는 가능한 위치에서 사용할 노란색 머티리얼입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placement Preview")
	TObjectPtr<UMaterialInterface> WarningPlacementMaterial;

private:
	// 현재 프리뷰 상태에 맞는 머티리얼을 모든 메시 슬롯에 적용합니다.
	void ApplyPreviewMaterial(ECSShelfPlacementPreviewState NewState);
};
