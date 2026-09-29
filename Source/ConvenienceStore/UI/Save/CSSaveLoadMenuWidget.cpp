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

	// 수동 슬롯(CS_Slot0~2)은 V2 1단계에서 숨기고 자동 세이브 슬롯만 표시
	// 2단계에서 세이브 파일 슬롯으로 재사용 예정
	const FName AutoSlotName = UCSSaveSubsystem::GetAutoSaveSlotName();
	CreateSlotWidget(AutoSlotName, FindHeader(AutoSlotName));

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

	// 수동 저장이 없으므로 저장 요청은 연결하지 않음
	SlotWidget->OnLoadRequested.AddUniqueDynamic(this, &UCSSaveLoadMenuWidget::HandleSlotLoadRequested);
	SlotWidget->OnDeleteRequested.AddUniqueDynamic(this, &UCSSaveLoadMenuWidget::HandleSlotDeleteRequested);

	SlotContainer->AddChild(SlotWidget);
	SlotWidgets.Add(SlotWidget);
	return SlotWidget;
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
