#include "UI/Save/CSSaveLoadMenuWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include "Systems/SaveLoad/CSSaveSubsystem.h"
#include "UI/Save/CSSaveSlotWidget.h"

void UCSSaveLoadMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(CloseButton))
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UCSSaveLoadMenuWidget::HandleCloseClicked);
	}

	RefreshSlots();
}

UCSSaveSubsystem* UCSSaveLoadMenuWidget::GetSaveSubsystem() const
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

void UCSSaveLoadMenuWidget::RefreshSlots()
{
	UCSSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (SaveSubsystem == nullptr || !IsValid(SlotContainer))
	{
		return;
	}

	SlotContainer->ClearChildren();
	SlotWidgets.Reset();

	// 전체 세이브 대신 헤더만 담은 메타 슬롯을 조회
	const TArray<FCSSaveHeader> Headers = SaveSubsystem->GetAllSlotHeaders();
	auto FindHeader = [&Headers](FName InSlotName) -> const FCSSaveHeader*
	{
		return Headers.FindByPredicate(
			[InSlotName](const FCSSaveHeader& Candidate)
			{
				return Candidate.SlotName == InSlotName;
			});
	};

	if (bShowAutoSaveSlot)
	{
		const FName AutoSlotName = UCSSaveSubsystem::GetAutoSaveSlotName();
		CreateSlotWidget(AutoSlotName, FindHeader(AutoSlotName));
	}

	for (int32 SlotIndex = 0; SlotIndex < UCSSaveSubsystem::GetManualSlotCount(); ++SlotIndex)
	{
		const FName ManualSlotName = UCSSaveSubsystem::MakeManualSlotName(SlotIndex);
		CreateSlotWidget(ManualSlotName, FindHeader(ManualSlotName));
	}

	SetStatusMessage(FText::GetEmpty());
}

UCSSaveSlotWidget* UCSSaveLoadMenuWidget::CreateSlotWidget(
	FName SlotName,
	const FCSSaveHeader* InHeader)
{
	if (SlotWidgetClass == nullptr || !IsValid(SlotContainer))
	{
		return nullptr;
	}

	UCSSaveSlotWidget* SlotWidget = CreateWidget<UCSSaveSlotWidget>(this, SlotWidgetClass);
	if (!IsValid(SlotWidget))
	{
		return nullptr;
	}

	if (InHeader != nullptr)
	{
		SlotWidget->SetupSlot(*InHeader);
	}
	else
	{
		SlotWidget->SetupEmptySlot(SlotName);
	}

	// 자동 세이브 슬롯에는 수동 저장 요청을 연결하지 않음
	if (SlotName != UCSSaveSubsystem::GetAutoSaveSlotName())
	{
		SlotWidget->OnSaveRequested.AddUniqueDynamic(this, &UCSSaveLoadMenuWidget::HandleSlotSaveRequested);
	}

	SlotWidget->OnLoadRequested.AddUniqueDynamic(this, &UCSSaveLoadMenuWidget::HandleSlotLoadRequested);
	SlotWidget->OnDeleteRequested.AddUniqueDynamic(this, &UCSSaveLoadMenuWidget::HandleSlotDeleteRequested);

	SlotContainer->AddChild(SlotWidget);
	SlotWidgets.Add(SlotWidget);
	return SlotWidget;
}

void UCSSaveLoadMenuWidget::HandleSlotSaveRequested(FName SlotName)
{
	UCSSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (SaveSubsystem == nullptr)
	{
		return;
	}

	// 영업 중 저장은 불러올 때 배달 실패·평판 감소를 일으키므로 준비 단계에서만 허용
	FText BlockedReason;
	if (!SaveSubsystem->CanManualSave(BlockedReason))
	{
		SetStatusMessage(BlockedReason);
		return;
	}

	if (!SaveSubsystem->SaveToSlot(SlotName))
	{
		SetStatusMessage(NSLOCTEXT("Save", "SaveFailed", "저장에 실패했습니다."));
		return;
	}

	// 목록을 먼저 갱신한 뒤 결과 메시지를 표시
	RefreshSlots();
	SetStatusMessage(FText::Format(
		NSLOCTEXT("Save", "SaveSucceeded", "{0}에 저장했습니다."),
		FText::FromName(SlotName)));
}

void UCSSaveLoadMenuWidget::HandleSlotLoadRequested(FName SlotName)
{
	UCSSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (SaveSubsystem == nullptr)
	{
		return;
	}

	// 성공하면 레벨이 다시 열리며 메뉴도 함께 제거
	if (!SaveSubsystem->LoadFromSlot(SlotName))
	{
		SetStatusMessage(NSLOCTEXT("Save", "LoadFailed",
			"불러오기에 실패했습니다. 지원하지 않는 버전이거나 파일이 손상되었습니다."));
	}
}

void UCSSaveLoadMenuWidget::HandleSlotDeleteRequested(FName SlotName)
{
	UCSSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (SaveSubsystem == nullptr)
	{
		return;
	}

	SaveSubsystem->DeleteSlot(SlotName);

	// 목록을 먼저 갱신한 뒤 결과 메시지를 표시
	RefreshSlots();
	SetStatusMessage(FText::Format(
		NSLOCTEXT("Save", "SlotDeleted", "{0}을 삭제했습니다."),
		FText::FromName(SlotName)));
}

void UCSSaveLoadMenuWidget::HandleCloseClicked()
{
	OnMenuClosed.Broadcast();
}

void UCSSaveLoadMenuWidget::SetStatusMessage(const FText& InMessage)
{
	if (IsValid(StatusText))
	{
		StatusText->SetText(InMessage);
	}
}
