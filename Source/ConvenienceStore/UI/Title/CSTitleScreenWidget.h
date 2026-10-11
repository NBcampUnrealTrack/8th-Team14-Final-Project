#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CSTitleScreenWidget.generated.h"

class UButton;
class UCSTitleSaveMenuWidget;

// 타이틀 맵의 첫 화면
// 게임 시작 시 세이브 파일 선택 메뉴를 열고 종료 시 게임을 종료
UCLASS(Abstract)
class CONVENIENCESTORE_API UCSTitleScreenWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	// 게임 시작 버튼 클릭 시 세이브 파일 선택 메뉴를 표시
	UFUNCTION()
	void HandleStartClicked();

	// 종료 버튼 클릭 시 게임을 종료
	UFUNCTION()
	void HandleQuitClicked();

	// 세이브 파일 선택 메뉴의 닫기 요청을 받아 메뉴를 제거
	UFUNCTION()
	void HandleSaveMenuClosed();

	// 게임 시작 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> BTN_Start;

	// 게임 종료 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> BTN_Quit;

	// 게임 시작 시 생성할 세이브 파일 선택 메뉴 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Title")
	TSubclassOf<UCSTitleSaveMenuWidget> SaveMenuClass;

	// 현재 화면에 표시 중인 세이브 파일 선택 메뉴
	UPROPERTY(Transient)
	TObjectPtr<UCSTitleSaveMenuWidget> ActiveSaveMenu;
};
