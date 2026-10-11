#include "UI/Title/CSTitleSaveSlotCardWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"

void UCSTitleSaveSlotCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 카드 본문과 하단 버튼에 같은 요청 처리를 연결
	Btn_Card->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveSlotCardWidget::HandlePrimaryClicked);
	Btn_PrimaryAction->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveSlotCardWidget::HandlePrimaryClicked);
	Btn_Delete->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveSlotCardWidget::HandleDeleteClicked);

	SetSelectedVisible(false);
}

void UCSTitleSaveSlotCardWidget::SetupCard(FName InSlotName, const FCSSaveHeader* InHeader)
{
	SlotName = InSlotName;
	bIsEmptySlot = InHeader == nullptr || !InHeader->IsValidHeader();

	// 빈 슬롯은 빈 슬롯 화면으로 전환하고 지울 기록이 없으므로 삭제 버튼을 숨김
	if (bIsEmptySlot)
	{
		WS_SlotState->SetActiveWidgetIndex(EmptyIndex);
		Btn_Delete->SetVisibility(ESlateVisibility::Collapsed);

		if (IsValid(Txt_PrimaryAction))
		{
			Txt_PrimaryAction->SetText(NSLOCTEXT("TitleSave", "NewGame", "새로 시작"));
		}
		return;
	}

	// 저장 슬롯은 저장 정보 화면으로 전환하고 삭제 버튼을 표시
	WS_SlotState->SetActiveWidgetIndex(FilledIndex);
	Btn_Delete->SetVisibility(ESlateVisibility::Visible);

	if (IsValid(Txt_PrimaryAction))
	{
		Txt_PrimaryAction->SetText(NSLOCTEXT("TitleSave", "Continue", "이어하기"));
	}

	const FCSSaveHeader& Header = *InHeader;

	if (IsValid(Txt_Day))
	{
		Txt_Day->SetText(FText::Format(
			NSLOCTEXT("TitleSave", "DayFormat", "{0}일차"),
			FText::AsNumber(Header.BusinessDay)));
	}

	if (IsValid(Txt_DateTime))
	{
		// AsDateTime은 입력을 UTC로 보고 지역 시간대로 변환하므로 UTC 값을 그대로 전달
		Txt_DateTime->SetText(FText::AsDateTime(Header.SavedAtUtc));
	}

	if (IsValid(Txt_Money))
	{
		Txt_Money->SetText(FText::Format(
			NSLOCTEXT("TitleSave", "MoneyFormat", "{0}G"),
			FText::AsNumber(Header.Funds)));
	}

	if (IsValid(Txt_StoreLevel))
	{
		Txt_StoreLevel->SetText(FText::Format(
			NSLOCTEXT("TitleSave", "StoreLevelFormat", "LV.{0}"),
			FText::AsNumber(Header.StoreLevel)));
	}

	if (IsValid(Txt_PlayTime))
	{
		// 초 단위 플레이 시간을 시간과 분으로 변환해 표시
		const int32 TotalMinutes = FMath::Max(0, FMath::FloorToInt(Header.PlayTimeSeconds / 60.0f));
		Txt_PlayTime->SetText(FText::Format(
			NSLOCTEXT("TitleSave", "PlayTimeFormat", "{0}시간 {1}분"),
			FText::AsNumber(TotalMinutes / 60),
			FText::AsNumber(TotalMinutes % 60)));
	}
}

void UCSTitleSaveSlotCardWidget::HandlePrimaryClicked()
{
	OnPrimaryRequested.Broadcast(SlotName, bIsEmptySlot);
}

void UCSTitleSaveSlotCardWidget::HandleDeleteClicked()
{
	// 빈 슬롯은 삭제 요청을 무시
	if (bIsEmptySlot)
	{
		return;
	}

	OnDeleteRequested.Broadcast(SlotName);
}

void UCSTitleSaveSlotCardWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	SetSelectedVisible(true);
}

void UCSTitleSaveSlotCardWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	SetSelectedVisible(false);
}

void UCSTitleSaveSlotCardWidget::SetSelectedVisible(bool bVisible)
{
	if (!IsValid(Img_Card_Selected))
	{
		return;
	}

	// 선택 테두리가 버튼 클릭을 가로채지 않도록 히트 테스트 없이 표시
	if (bVisible)
	{
		Img_Card_Selected->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	else
	{
		Img_Card_Selected->SetVisibility(ESlateVisibility::Hidden);
	}
}
