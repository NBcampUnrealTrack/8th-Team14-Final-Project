#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSInnerThoughtWidget.generated.h"

class UTextBlock;

// 마법봉으로 거절당한 손님을 맞혔을 때 뜨는 속마음 위젯
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSInnerThoughtWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 속마음 문장을 설정. UCSPurchaseDecisionComponent::BuildInnerThoughtText()가 만든 완성 문장을 그대로 넘기면 됨
	UFUNCTION(BlueprintCallable, Category = "Customer|InnerThought")
	void SetThoughtText(const FText& InText);

	// 위젯을 표시하고 페이드 인/슬라이드 인을 시작합니다.
	void ShowAnimated();

	// 페이드 아웃/슬라이드 아웃이 끝난 뒤 위젯을 숨깁니다.
	void HideAnimated();

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// WBP에서 같은 이름("ThoughtText")의 TextBlock을 만들면 C++에서 자동으로 연결됨
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ThoughtText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|InnerThought|Animation", meta = (ClampMin = "0.01"))
	float ShowAnimationDuration = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|InnerThought|Animation", meta = (ClampMin = "0.01"))
	float HideAnimationDuration = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Customer|InnerThought|Animation", meta = (ClampMin = "0.0"))
	float AnimationVerticalOffset = 24.0f;

private:
	enum class ETransitionState : uint8
	{
		None,
		Showing,
		Hiding
	};

	ETransitionState TransitionState = ETransitionState::None;
	float TransitionElapsed = 0.0f;
};
