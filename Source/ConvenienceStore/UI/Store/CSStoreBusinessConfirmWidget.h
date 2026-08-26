#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSStoreBusinessConfirmWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnStoreBusinessConfirmed, bool, bShouldOpen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnStoreBusinessConfirmCancelled);

UENUM(BlueprintType)
enum class ECSStoreBusinessConfirmType : uint8
{
	InitialOpen,
	PauseEntry,
	ResumeEntry
};

// 최초 개점과 고객 입장 중단·재개를 한 번 더 확인하는 공통 확인 창입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSStoreBusinessConfirmWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetConfirmationType(ECSStoreBusinessConfirmType InConfirmationType);

	UPROPERTY(BlueprintAssignable, Category = "Store|Business")
	FCSOnStoreBusinessConfirmed OnBusinessConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "Store|Business")
	FCSOnStoreBusinessConfirmCancelled OnBusinessCancelled;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> QuestionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GuideText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ConfirmButtonText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConfirmButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	UFUNCTION()
	void HandleConfirmClicked();

	UFUNCTION()
	void HandleCancelClicked();

	bool bShouldOpen = false;
	ECSStoreBusinessConfirmType ConfirmationType = ECSStoreBusinessConfirmType::InitialOpen;
};
