#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSSaveTypes.h"
#include "CSSaveLoadMenuWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UCSSaveSlotWidget;
class UCSSaveSubsystem;

// 닫기 버튼 클릭 알림
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnSaveLoadMenuClosed);

// 자동 세이브와 수동 슬롯 3개를 함께 표시하는 저장·불러오기 메뉴
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSSaveLoadMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 메타 슬롯을 다시 읽어 슬롯 목록을 재구성
	UFUNCTION(BlueprintCallable, Category = "Save|Menu")
	void RefreshSlots();

	// 닫기 요청 알림
	UPROPERTY(BlueprintAssignable, Category = "Save|Menu")
	FCSOnSaveLoadMenuClosed OnMenuClosed;

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	void HandleSlotSaveRequested(FName SlotName);

	UFUNCTION()
	void HandleSlotLoadRequested(FName SlotName);

	UFUNCTION()
	void HandleSlotDeleteRequested(FName SlotName);

	UFUNCTION()
	void HandleCloseClicked();

	// 게임 인스턴스의 세이브 서브시스템을 조회
	UCSSaveSubsystem* GetSaveSubsystem() const;

	// 슬롯 위젯을 생성해 컨테이너에 추가하고 반환
	UCSSaveSlotWidget* CreateSlotWidget(FName SlotName, const FCSSaveHeader* InHeader);

	// 하단 결과 메시지를 설정
	void SetStatusMessage(const FText& InMessage);

	// 슬롯 위젯을 배치할 컨테이너
	// 지정하지 않으면 슬롯을 생성하지 않음
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> SlotContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	// 슬롯 한 칸을 표시할 위젯 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save|Menu")
	TSubclassOf<UCSSaveSlotWidget> SlotWidgetClass;

	// 자동 세이브 슬롯의 목록 표시 여부
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save|Menu")
	bool bShowAutoSaveSlot = true;

	// 현재 생성된 슬롯 위젯 목록
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCSSaveSlotWidget>> SlotWidgets;
};
