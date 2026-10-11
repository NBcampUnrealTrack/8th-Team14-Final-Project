#include "UI/Title/CSTitleScreenWidget.h"

#include "Components/Button.h"
#include "Kismet/KismetSystemLibrary.h"

#include "UI/Title/CSTitleSaveMenuWidget.h"

void UCSTitleScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BTN_Start->OnClicked.AddUniqueDynamic(this, &UCSTitleScreenWidget::HandleStartClicked);
	BTN_Quit->OnClicked.AddUniqueDynamic(this, &UCSTitleScreenWidget::HandleQuitClicked);
}

void UCSTitleScreenWidget::HandleStartClicked()
{
	// 메뉴가 이미 열려 있거나 메뉴 클래스가 지정되지 않으면 중복 생성을 방지
	if (IsValid(ActiveSaveMenu) || SaveMenuClass == nullptr)
	{
		return;
	}

	ActiveSaveMenu = CreateWidget<UCSTitleSaveMenuWidget>(GetOwningPlayer(), SaveMenuClass);
	if (!IsValid(ActiveSaveMenu))
	{
		return;
	}

	// 타이틀 화면 위에 표시되도록 높은 ZOrder로 추가
	ActiveSaveMenu->OnMenuClosed.AddUniqueDynamic(this, &UCSTitleScreenWidget::HandleSaveMenuClosed);
	ActiveSaveMenu->AddToViewport(10);
}

void UCSTitleScreenWidget::HandleSaveMenuClosed()
{
	// 바인딩을 해제한 뒤 메뉴를 화면에서 제거
	if (IsValid(ActiveSaveMenu))
	{
		ActiveSaveMenu->OnMenuClosed.RemoveDynamic(this, &UCSTitleScreenWidget::HandleSaveMenuClosed);
		ActiveSaveMenu->RemoveFromParent();
	}

	ActiveSaveMenu = nullptr;
}

void UCSTitleScreenWidget::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
