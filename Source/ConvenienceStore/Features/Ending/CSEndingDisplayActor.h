#pragma once

#include "CoreMinimal.h"
#include "Features/Ending/CSEndingActors.h"
#include "CSEndingDisplayActor.generated.h"

class UPrimitiveComponent;
class UMaterialInterface;

// 같은 슬롯 구조를 마녀의 제출 기록 전시와 가게의 실물 보관에 사용합니다.
UCLASS()
class CONVENIENCESTORE_API ACSEndingDisplayActor : public ACSEndingInteractionActor
{
	GENERATED_BODY()
public:
	ACSEndingDisplayActor();
	// 참이면 제출 기록으로 회수 불가능한 복제 외형을 표시합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ending|Display") bool bSubmittedDisplay = false;
	// 각 박스의 중심이 해당 재료의 전시 위치이며, 데이터 배열 순서와 대응합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ending|Display") TObjectPtr<UBoxComponent> CompassSlot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ending|Display") TObjectPtr<UBoxComponent> ResonanceStoneSlot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ending|Display") TObjectPtr<UBoxComponent> BoundaryCoreSlot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ending|Display") TObjectPtr<UBoxComponent> ReturnCoreSlot;

	FText GetSlotPrompt(const ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent) const;
	void InteractWithSlot(ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent);
	// Q는 배치만, E는 빈손 회수만 처리합니다.
	void PlaceInSlot(ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent);
	void UpdatePlacementPreview(ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent);
	void HidePlacementPreview();
	UPROPERTY(EditDefaultsOnly, Category = "Ending|Preview") TObjectPtr<UMaterialInterface> ValidPreviewMaterial;
	UPROPERTY(EditDefaultsOnly, Category = "Ending|Preview") TObjectPtr<UMaterialInterface> InvalidPreviewMaterial;
	virtual FText GetInteractionText_Implementation() const override;
	// 제출 기록을 복원하거나 전시 외형을 다시 생성해야 할 때도 호출할 수 있습니다.
	UFUNCTION(BlueprintCallable, Category = "Ending|Display") void RefreshSubmittedDisplay();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UBoxComponent* GetSlot(int32 Index) const;
	int32 FindSlot(const UPrimitiveComponent* Component) const;
	void AttachDisplayItem(ACSEndingItemActor* Item, int32 Index);
	UPROPERTY(Transient) TArray<TObjectPtr<ACSEndingItemActor>> DisplayedItems;
	// 회수할 때 전시용 배율이 손에 든 아이템에 남지 않도록 원래 배율을 보관합니다.
	TArray<FVector> OriginalScales;
	// 회수 시 기존 부모 스케일 상속 설정도 복구합니다.
	TArray<bool> OriginalAbsoluteScaleFlags;
	bool bUpdatingDisplay = false;
	bool CanPlace(const ACSPlayerCharacter* Player, int32 Index) const;
	UPROPERTY(Transient) TObjectPtr<AActor> PlacementPreview;
	TWeakObjectPtr<ACSEndingItemActor> PreviewSource;
};
