#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "CSTutorialMarkerComponent.generated.h"

class UCSTutorialMarkerWidgetBase;
class UPrimitiveComponent;

// 튜토리얼 대상 액터(쓰레기, 수정구슬 등)에 붙여서 사용하는 3D 마커
// 플레이어와의 거리에 따라 자동으로 옅어지고 진해짐
UCLASS(ClassGroup = (Tutorial), meta = (BlueprintSpawnableComponent))
class CONVENIENCESTORE_API UCSTutorialMarkerComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UCSTutorialMarkerComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// 마커를 켜고 라벨 문구를 표시
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void ShowMarker(const FText& Label);

	// 마커를 끔
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void HideMarker();

protected:
	// 이 거리 이내면 완전히 선명하게 보임
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "0.0"))
	float FadeStartDistance = 300.0f;

	// 이 거리를 넘으면 완전히 투명
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "0.0"))
	float FadeEndDistance = 900.0f;

	// 마커를 켤 때 소유 액터의 메시 외곽선(CustomDepth)도 같이 켤지 여부
	// 라벨만 필요하고 빛나는 효과는 필요 없는 대상이면 false로 끌 수 있음
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	bool bHighlightOwnerOutline = true;

	// 외곽선에 쓸 스텐실 값
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (ClampMin = "0"))
	int32 OutlineStencilValue = 3;

private:
	// 현재 플레이어와의 거리를 바탕으로 0(완전 투명)~1(완전 선명) 사이 값을 계산
	float ComputeDistanceAlpha() const;

	// 외곽선을 켜기 전 원래 상태를 기억해뒀다가 끌 때 복원
	TMap<TObjectPtr<UPrimitiveComponent>, bool> OriginalCustomDepthStates;
	TMap<TObjectPtr<UPrimitiveComponent>, int32> OriginalCustomStencilValues;

	void ApplyOwnerOutline();
	void RestoreOwnerOutline();

	// 켜져 있을 때만 매 틱 거리 계산을 함
	// 꺼져 있으면 계산 자체를 건너뜀
	bool bIsMarkerActive = false;
};
