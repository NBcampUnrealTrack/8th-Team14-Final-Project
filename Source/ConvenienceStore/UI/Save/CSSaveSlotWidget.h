#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSSaveTypes.h"
#include "CSSaveSlotWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnSaveSlotSaveRequested, FName, SlotName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnSaveSlotLoadRequested, FName, SlotName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnSaveSlotDeleteRequested, FName, SlotName);

// 세이브 슬롯 한 칸을 표시하는 위젯
// 전체 세이브 대신 경량 헤더만 받아 표시
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSSaveSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 비어 있는 슬롯으로 표시를 설정
	UFUNCTION(BlueprintCallable, Category = "Save|Slot")
	void SetupEmptySlot(FName InSlotName);

	// 헤더 정보로 슬롯 표시를 설정
	UFUNCTION(BlueprintCallable, Category = "Save|Slot")
	void SetupSlot(const FCSSaveHeader& InHeader);

	// 표시 중인 슬롯 이름을 반환
	UFUNCTION(BlueprintPure, Category = "Save|Slot")
	FName GetSlotName() const { return SlotName; }

	// 비어 있는 슬롯인지 확인
	UFUNCTION(BlueprintPure, Category = "Save|Slot")
	bool IsEmptySlot() const { return bIsEmptySlot; }

	// 저장 버튼 클릭 알림
	UPROPERTY(BlueprintAssignable, Category = "Save|Slot")
	FCSOnSaveSlotSaveRequested OnSaveRequested;

	// 불러오기 버튼 클릭 알림
	UPROPERTY(BlueprintAssignable, Category = "Save|Slot")
	FCSOnSaveSlotLoadRequested OnLoadRequested;

	// 삭제 버튼 클릭 알림
	UPROPERTY(BlueprintAssignable, Category = "Save|Slot")
	FCSOnSaveSlotDeleteRequested OnDeleteRequested;

protected:
	virtual void NativeConstruct() override;

	// 헤더 정보로 텍스트와 버튼 활성 상태를 갱신
	// 바인딩되지 않은 위젯 요소는 건너뜀
	void RefreshDisplay();

	UFUNCTION()
	void HandleSaveClicked();

	UFUNCTION()
	void HandleLoadClicked();

	UFUNCTION()
	void HandleDeleteClicked();

	// 플레이 시간(초)을 시간·분 표시 텍스트로 변환
	static FText MakePlayTimeText(float InPlayTimeSeconds);

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SlotTitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BusinessDayText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FundsText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StoreLevelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PlayTimeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SavedAtText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> SaveButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> LoadButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> DeleteButton;

	// 표시 중인 슬롯 헤더
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Save|Slot")
	FCSSaveHeader Header;

	// 표시 중인 슬롯 이름
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Save|Slot")
	FName SlotName;

	// 저장 데이터가 없는 슬롯 여부
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Save|Slot")
	bool bIsEmptySlot = true;
};
