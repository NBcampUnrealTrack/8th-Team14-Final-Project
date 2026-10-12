#include "UI/Title/CSTitleSaveMenuWidget.h"

#include "Components/Button.h"
#include "Components/AudioComponent.h"
#include "Components/PanelWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Systems/SaveLoad/CSSaveSubsystem.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "UI/Title/CSTitleSaveSlotCardWidget.h"

void UCSTitleSaveMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	Btn_Close->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveMenuWidget::HandleCloseClicked);
	Btn_DeleteConfirm->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveMenuWidget::HandleDeleteConfirmClicked);
	Btn_DeleteCancel->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveMenuWidget::HandleDeleteCancelClicked);

	// 카드 3장의 요청을 메뉴에서 일괄 처리하도록 연결
	for (UCSTitleSaveSlotCardWidget* Card : { WBP_SaveSlotCard.Get(), WBP_SaveSlotCard_1.Get(), WBP_SaveSlotCard_2.Get() })
	{
		if (IsValid(Card))
		{
			Card->OnPrimaryRequested.AddUniqueDynamic(this, &UCSTitleSaveMenuWidget::HandleSlotPrimaryRequested);
			Card->OnDeleteRequested.AddUniqueDynamic(this, &UCSTitleSaveMenuWidget::HandleSlotDeleteRequested);
		}
	}

	bIsTransitioning = false;
	PendingDeleteSlotName = NAME_None;
	SetDeletePopupVisible(false);
	RefreshSlots();
}

UCSSaveSubsystem* UCSTitleSaveMenuWidget::GetSaveSubsystem() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	UGameInstance* OwningGameInstance = World->GetGameInstance();
	if (OwningGameInstance == nullptr)
	{
		return nullptr;
	}

	return OwningGameInstance->GetSubsystem<UCSSaveSubsystem>();
}

void UCSTitleSaveMenuWidget::RefreshSlots()
{
	UCSSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (SaveSubsystem == nullptr)
	{
		return;
	}

	// 카드 순서대로 CS_Slot0~2의 헤더를 조회해 표시
	UCSTitleSaveSlotCardWidget* Cards[] = { WBP_SaveSlotCard, WBP_SaveSlotCard_1, WBP_SaveSlotCard_2 };
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Cards)); ++Index)
	{
		if (!IsValid(Cards[Index]))
		{
			continue;
		}

		const FName SlotName = UCSSaveSubsystem::GetFileSlotName(Index);

		// 메타 헤더가 남아 있어도 실제 슬롯 파일이 없으면 빈 슬롯으로 표시
		FCSSaveHeader Header;
		const FCSSaveHeader* FoundHeader = nullptr;
		if (SaveSubsystem->DoesSlotExist(SlotName) && SaveSubsystem->GetSlotHeader(SlotName, Header))
		{
			FoundHeader = &Header;
		}

		Cards[Index]->SetupCard(SlotName, FoundHeader);
	}
}

void UCSTitleSaveMenuWidget::HandleSlotPrimaryRequested(FName SlotName, bool bIsEmptySlot)
{
	UCSSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (SaveSubsystem == nullptr || bIsTransitioning)
	{
		return;
	}

	// 다음 맵의 PlayerController가 입력 모드를 다시 설정하지만 커서 잔상을 방지하기 위해 미리 해제
	if (APlayerController* OwningPlayer = GetOwningPlayer())
	{
		OwningPlayer->SetInputMode(FInputModeGameOnly());
		OwningPlayer->SetShowMouseCursor(false);
	}

	// 빈 슬롯은 새 게임으로, 저장 슬롯은 저장 시점부터 이어서 진행
	if (bIsEmptySlot)
	{
		bIsTransitioning = SaveSubsystem->StartNewGame(SlotName, GameLevel);
	}
	else
	{
		bIsTransitioning = SaveSubsystem->LoadFromSlot(SlotName);
	}

	if (bIsTransitioning)
	{
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this))
		{
			Audio->StopBGM();
			if (UAudioComponent* StartSound = Audio->CreateTrackedSFX2D(ECSAudioEvent::GameStart, true))
			{
				StartSound->Play();
			}
		}
	}

	// 버전 불일치나 파일 손상으로 실패하면 메뉴를 다시 사용할 수 있도록 복구
	if (!bIsTransitioning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CSSave] 타이틀 슬롯 진입에 실패했습니다: %s"), *SlotName.ToString());
		RestoreUIInput();
		RefreshSlots();
	}
}

void UCSTitleSaveMenuWidget::HandleSlotDeleteRequested(FName SlotName)
{
	// 레벨 전환 중에는 삭제 요청을 무시
	if (bIsTransitioning)
	{
		return;
	}

	PendingDeleteSlotName = SlotName;
	SetDeletePopupVisible(true);
}

void UCSTitleSaveMenuWidget::HandleDeleteConfirmClicked()
{
	// 본 슬롯과 백업 슬롯, 메타 헤더를 함께 제거
	UCSSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (SaveSubsystem != nullptr && !PendingDeleteSlotName.IsNone())
	{
		SaveSubsystem->DeleteSlot(PendingDeleteSlotName);
	}

	PendingDeleteSlotName = NAME_None;
	SetDeletePopupVisible(false);
	RefreshSlots();
}

void UCSTitleSaveMenuWidget::HandleDeleteCancelClicked()
{
	PendingDeleteSlotName = NAME_None;
	SetDeletePopupVisible(false);
}

void UCSTitleSaveMenuWidget::HandleCloseClicked()
{
	// 레벨 전환 중에는 닫기 요청을 무시
	if (bIsTransitioning)
	{
		return;
	}

	OnMenuClosed.Broadcast();
}

void UCSTitleSaveMenuWidget::SetDeletePopupVisible(bool bVisible)
{
	// 팝업 표시 중 뒤쪽 카드 클릭을 막도록 히트 테스트가 되는 Visible로 설정
	if (bVisible)
	{
		Overlay_DeletePopup->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		Overlay_DeletePopup->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCSTitleSaveMenuWidget::RestoreUIInput()
{
	APlayerController* OwningPlayer = GetOwningPlayer();
	if (OwningPlayer == nullptr)
	{
		return;
	}

	// 메뉴에 포커스를 두고 마우스 커서를 다시 표시
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	OwningPlayer->SetInputMode(InputMode);
	OwningPlayer->SetShowMouseCursor(true);
}
