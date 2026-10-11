#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTitleSaveMenuWidget.generated.h"

class UButton;
class UPanelWidget;
class UCSSaveSubsystem;
class UCSTitleSaveSlotCardWidget;
class UWorld;

// 닫기 버튼 클릭 시 방송
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnTitleSaveMenuClosed);

// 타이틀에서 여는 세이브 파일 선택 메뉴
// 빈 슬롯은 새 게임, 저장 슬롯은 이어하기로 처리하고 삭제는 확인 팝업을 거침
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTitleSaveMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 세이브 파일 슬롯 헤더를 다시 조회해 카드 3장을 갱신
	UFUNCTION(BlueprintCallable, Category = "Save|TitleMenu")
	void RefreshSlots();

	// 메뉴 닫기 요청 알림
	UPROPERTY(BlueprintAssignable, Category = "Save|TitleMenu")
	FCSOnTitleSaveMenuClosed OnMenuClosed;

protected:
	virtual void NativeConstruct() override;

	// 카드의 새로 시작·이어하기 요청을 처리
	UFUNCTION()
	void HandleSlotPrimaryRequested(FName SlotName, bool bIsEmptySlot);

	// 카드의 삭제 요청을 받아 확인 팝업을 표시
	UFUNCTION()
	void HandleSlotDeleteRequested(FName SlotName);

	// 삭제 팝업의 확인 버튼 클릭을 처리
	UFUNCTION()
	void HandleDeleteConfirmClicked();

	// 삭제 팝업의 취소 버튼 클릭을 처리
	UFUNCTION()
	void HandleDeleteCancelClicked();

	// 닫기 버튼 클릭을 처리
	UFUNCTION()
	void HandleCloseClicked();

	// 게임 인스턴스의 세이브 서브시스템을 조회
	UCSSaveSubsystem* GetSaveSubsystem() const;

	// 삭제 확인 팝업의 표시 여부를 설정
	void SetDeletePopupVisible(bool bVisible);

	// 레벨 전환 실패 시 타이틀 UI 입력과 마우스 커서를 복구
	void RestoreUIInput();

	// 새 게임 시작 시 열 본 게임 레벨
	// 이어하기는 세이브 헤더에 기록된 레벨을 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save|TitleMenu")
	TSoftObjectPtr<UWorld> GameLevel;

	// CS_Slot0을 표시하는 첫 번째 카드
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCSTitleSaveSlotCardWidget> WBP_SaveSlotCard;

	// CS_Slot1을 표시하는 두 번째 카드
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCSTitleSaveSlotCardWidget> WBP_SaveSlotCard_1;

	// CS_Slot2를 표시하는 세 번째 카드
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCSTitleSaveSlotCardWidget> WBP_SaveSlotCard_2;

	// 메뉴 닫기 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Btn_Close;

	// 슬롯 삭제 확인 팝업
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> Overlay_DeletePopup;

	// 삭제 팝업의 삭제 확인 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Btn_DeleteConfirm;

	// 삭제 팝업의 취소 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Btn_DeleteCancel;

	// 삭제 팝업이 확인을 기다리는 슬롯 이름
	FName PendingDeleteSlotName;

	// 레벨 전환 요청이 성공해 추가 입력을 무시하는 상태인지 여부
	bool bIsTransitioning = false;
};
