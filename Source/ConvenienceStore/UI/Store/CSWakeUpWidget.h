#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSWakeUpWidget.generated.h"

class UCurveFloat;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnEyeOpeningFinished);

// 이 클래스를 부모로 위젯 블루프린트를 만드시면 기상 연출을 수정하실 수 있습니다.
UCLASS()
class CONVENIENCESTORE_API UCSWakeUpWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Store|Wake Up")
	FCSOnEyeOpeningFinished OnEyeOpeningFinished;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Wake Up", meta = (ClampMin = "0.1"))
	float EyeOpeningDuration = 1.5f;

	// 가로축은 진행도 0~1, 세로축은 눈을 뜬 정도 0~1로 지정해 주세요.
	// 곡선을 비워 두시면 살짝 눈을 떴다가 한 번 감고 완전히 뜹니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Wake Up")
	TObjectPtr<UCurveFloat> EyeOpeningCurve;

	// 화면 중앙의 눈꺼풀 경계에 사용할 진한 색과 투명도를 지정합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Wake Up|Appearance")
	FLinearColor CenterLidColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	// 화면 바깥 가장자리에 사용할 옅은 색과 투명도를 지정합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Wake Up|Appearance")
	FLinearColor OuterLidColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.18f);

	// 값이 클수록 중앙에서 바깥으로 이어지는 그라데이션이 부드러워집니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Wake Up|Appearance",
		meta = (ClampMin = "2", ClampMax = "32"))
	int32 LidGradientSteps = 16;

private:
	float Elapsed = 0.0f;
	double LastRealTime = 0.0;
	float EyeOpenness = 0.0f;
	bool bFinished = false;
};
