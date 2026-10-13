#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/CSSaveTypes.h"
#include "CSTitleSaveSlotCardWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UWidgetSwitcher;

// 하단 버튼 클릭 시 슬롯 이름과 빈 슬롯 여부를 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSOnTitleSlotPrimaryRequested, FName, SlotName, bool, bIsEmptySlot);

// 삭제 버튼 클릭 시 슬롯 이름을 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSOnTitleSlotDeleteRequested, FName, SlotName);

// 타이틀 세이브 메뉴에서 세이브 파일 슬롯 한 칸을 표시하는 카드
// 빈 슬롯은 새로 시작, 저장 슬롯은 이어하기 요청을 방송
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTitleSaveSlotCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 헤더 정보로 카드 표시를 갱신
	// 헤더가 없거나 비어 있으면 빈 슬롯으로 설정
	void SetupCard(FName InSlotName, const FCSSaveHeader* InHeader);

	// 새로 시작·이어하기 요청 알림
	UPROPERTY(BlueprintAssignable, Category = "Save|TitleSlot")
	FCSOnTitleSlotPrimaryRequested OnPrimaryRequested;

	// 슬롯 삭제 요청 알림
	UPROPERTY(BlueprintAssignable, Category = "Save|TitleSlot")
	FCSOnTitleSlotDeleteRequested OnDeleteRequested;

protected:
	virtual void NativeConstruct() override;

	//윤서희 수정 카드 호버가 시작되면 선택 효과와 시작 버튼을 활성화
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	//윤서희 수정 카드 호버가 끝나면 선택 효과와 시작 버튼을 비활성화
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	// 하단 버튼 클릭을 처리
	UFUNCTION()
	void HandlePrimaryClicked();

	// 삭제 버튼 클릭을 처리
	UFUNCTION()
	void HandleDeleteClicked();

	// 선택 테두리 이미지의 표시 여부를 설정
	void SetSelectedVisible(bool bVisible);

	//윤서희 수정 카드 호버에 맞춰 선택 효과와 시작 버튼 상태를 함께 변경
	void SetHoverActive(bool bActive);

	// 저장 슬롯 표시에 사용하는 위젯 스위처 자식 인덱스
	static constexpr int32 FilledIndex = 0;

	// 빈 슬롯 표시에 사용하는 위젯 스위처 자식 인덱스
	static constexpr int32 EmptyIndex = 1;

	// 저장 슬롯과 빈 슬롯 표시를 전환할 위젯 스위처
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> WS_SlotState;

	// 카드 전체 클릭 영역
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Btn_Card;

	// 우상단 삭제 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Btn_Delete;

	// 하단 새로 시작·이어하기 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Btn_PrimaryAction;

	// 마우스를 올렸을 때 표시할 선택 테두리
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Card_Selected;

	// 저장 시점의 영업일 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Day;

	// 저장 시각 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DateTime;

	// 저장 시점의 보유 자금 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Money;

	// 저장 시점의 상점 레벨 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_StoreLevel;

	// 누적 플레이 시간 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_PlayTime;

	// 하단 버튼의 새로 시작·이어하기 문구
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_PrimaryAction;

	// 이 카드가 표시하는 세이브 파일 슬롯 이름
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Save|TitleSlot")
	FName SlotName;

	// 저장 기록이 없는 슬롯인지 여부
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Save|TitleSlot")
	bool bIsEmptySlot = true;

	//윤서희 수정 하단 시작 버튼이 현재 호버로 활성화된 상태인지 여부
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Save|TitleSlot")
	bool bIsHoverActive = false;
};
