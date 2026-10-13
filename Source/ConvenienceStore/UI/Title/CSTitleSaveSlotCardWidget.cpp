#include "UI/Title/CSTitleSaveSlotCardWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"

void UCSTitleSaveSlotCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	//윤서희 수정 카드 본문 클릭에는 실행을 연결하지 않고 하단 버튼만 게임 시작을 요청
	Btn_PrimaryAction->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveSlotCardWidget::HandlePrimaryClicked);
	Btn_Delete->OnClicked.AddUniqueDynamic(this, &UCSTitleSaveSlotCardWidget::HandleDeleteClicked);

	SetHoverActive(false);
}

void UCSTitleSaveSlotCardWidget::SetupCard(FName InSlotName, const FCSSaveHeader* InHeader)
{
	SlotName = InSlotName;
	bIsEmptySlot = InHeader == nullptr || !InHeader->IsValidHeader();

	//윤서희 수정 슬롯 정보가 갱신되면 다시 호버하기 전까지 시작 버튼을 비활성화
	SetHoverActive(false);

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

void UCSTitleSaveSlotCardWidget::SetHoverActive(bool bActive)
{
	//윤서희 수정 호버 중인 카드만 하단 시작 버튼을 누를 수 있도록 동기화
	bIsHoverActive = bActive;
	Btn_PrimaryAction->SetIsEnabled(bIsHoverActive);
	//윤서희 수정 비활성 버튼 영역도 뒤의 카드 버튼이 클릭되도록 히트 테스트를 통과시킴
	Btn_PrimaryAction->SetVisibility(bIsHoverActive ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	//윤서희 수정 기존 카드 호버 효과도 시작 버튼 활성 상태와 함께 표시
	SetSelectedVisible(bIsHoverActive);
}

void UCSTitleSaveSlotCardWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	//윤서희 수정 카드에 마우스를 올리면 호버 효과와 시작 버튼을 활성화
	SetHoverActive(true);
}

void UCSTitleSaveSlotCardWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	//윤서희 수정 카드에서 마우스가 벗어나면 호버 효과와 시작 버튼을 비활성화
	SetHoverActive(false);
}

void UCSTitleSaveSlotCardWidget::HandlePrimaryClicked()
{
	//윤서희 수정 호버로 활성화되지 않은 카드에서는 실행 요청을 보내지 않음
	if (!bIsHoverActive)
	{
		return;
	}

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
