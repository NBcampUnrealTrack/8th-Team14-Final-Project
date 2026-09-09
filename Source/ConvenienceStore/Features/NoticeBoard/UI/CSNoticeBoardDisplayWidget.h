#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSNoticeBoardDisplayWidget.generated.h"

class UCSDeliveryOrderSubsystem;

/** 게시판 표면에 항상 표시되는 월드 UI의 공통 기반 클래스입니다. */
UCLASS(Abstract, Blueprintable)
class CONVENIENCESTORE_API UCSNoticeBoardDisplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "Notice Board|Display")
	void RefreshDisplay();

	UFUNCTION(BlueprintPure, Category = "Notice Board|Display")
	bool HasPendingDeliveryOrder() const;

	/** 배달 대기 알림 룬의 표시 여부를 BP에서 갱신합니다. 날씨 표시는 이후 같은 이벤트에서 확장합니다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notice Board|Display")
	void RefreshNoticeBoardDisplay(bool bHasPendingDeliveryOrder);

protected:
	UFUNCTION()
	void HandleDeliveryOrdersChanged();

	UPROPERTY(Transient)
	TObjectPtr<UCSDeliveryOrderSubsystem> DeliveryOrderSubsystem;
};
