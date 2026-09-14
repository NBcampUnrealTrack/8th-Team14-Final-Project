#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSCustomerHeadIconWidget.generated.h"

class UImage;
class UTexture2D;

// 구매 거절/가격 반응 발생 시 손님 머리 위에 뜨는 아이콘 위젯
// DT_PurchaseRejectionDisplay Tiers.Icon을 그대로 받아 보여주기만 함 - 어떤 아이콘을 쓸지 판단은
// UCSPurchaseDecisionComponent::GetRejectionDisplayTier()가 이미 끝낸 상태로 넘어옴
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSCustomerHeadIconWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Tier.Icon을 그대로 넘기면 됨. 표시 여부(숨기기 포함)는 호출부(ACSCustomerCharacter)가 판단하므로
	// 여기서는 브러시 교체만 담당
	UFUNCTION(BlueprintCallable, Category = "Customer|HeadIcon")
	void SetIcon(UTexture2D* InIcon);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;
};
