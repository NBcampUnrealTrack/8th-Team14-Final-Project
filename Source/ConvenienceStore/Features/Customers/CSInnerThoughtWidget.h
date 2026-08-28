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

protected:
	// WBP에서 같은 이름("ThoughtText")의 TextBlock을 만들면 C++에서 자동으로 연결됨
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ThoughtText;
};
