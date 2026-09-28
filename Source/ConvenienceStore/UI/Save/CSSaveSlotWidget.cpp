#include "UI/Save/CSSaveSlotWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Internationalization/Text.h"

void UCSSaveSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(SaveButton))
	{
		SaveButton->OnClicked.AddUniqueDynamic(this, &UCSSaveSlotWidget::HandleSaveClicked);
	}

	if (IsValid(LoadButton))
	{
		LoadButton->OnClicked.AddUniqueDynamic(this, &UCSSaveSlotWidget::HandleLoadClicked);
	}

	if (IsValid(DeleteButton))
	{
		DeleteButton->OnClicked.AddUniqueDynamic(this, &UCSSaveSlotWidget::HandleDeleteClicked);
	}

	RefreshDisplay();
}

void UCSSaveSlotWidget::SetupEmptySlot(FName InSlotName)
{
	SlotName = InSlotName;
	Header = FCSSaveHeader();
	Header.SlotName = InSlotName;
	bIsEmptySlot = true;
	RefreshDisplay();
}

void UCSSaveSlotWidget::SetupSlot(const FCSSaveHeader& InHeader)
{
	Header = InHeader;
	SlotName = InHeader.SlotName;
	bIsEmptySlot = !InHeader.IsValidHeader();
	RefreshDisplay();
}

void UCSSaveSlotWidget::RefreshDisplay()
{
	// 자동 세이브 슬롯은 슬롯 이름 대신 고정 제목을 표시
	if (IsValid(SlotTitleText))
	{
		if (Header.bIsAutoSave)
		{
			SlotTitleText->SetText(NSLOCTEXT("Save", "AutoSlotTitle", "자동 저장"));
		}
		else
		{
			SlotTitleText->SetText(FText::FromName(SlotName));
		}
	}

	// 빈 슬롯은 요약 정보를 비우고 불러오기·삭제를 비활성화
	if (bIsEmptySlot)
	{
		const FText EmptyText = NSLOCTEXT("Save", "EmptySlot", "비어 있음");
		if (IsValid(BusinessDayText)) { BusinessDayText->SetText(EmptyText); }
		if (IsValid(FundsText)) { FundsText->SetText(FText::GetEmpty()); }
		if (IsValid(StoreLevelText)) { StoreLevelText->SetText(FText::GetEmpty()); }
		if (IsValid(PlayTimeText)) { PlayTimeText->SetText(FText::GetEmpty()); }
		if (IsValid(SavedAtText)) { SavedAtText->SetText(FText::GetEmpty()); }
		if (IsValid(LoadButton)) { LoadButton->SetIsEnabled(false); }
		if (IsValid(DeleteButton)) { DeleteButton->SetIsEnabled(false); }
		return;
	}

	if (IsValid(BusinessDayText))
	{
		BusinessDayText->SetText(FText::Format(
			NSLOCTEXT("Save", "BusinessDayFormat", "{0}일차"),
			FText::AsNumber(Header.BusinessDay)));
	}

	if (IsValid(FundsText))
	{
		FundsText->SetText(FText::Format(
			NSLOCTEXT("Save", "FundsFormat", "자금 {0}G"),
			FText::AsNumber(Header.Funds)));
	}

	if (IsValid(StoreLevelText))
	{
		StoreLevelText->SetText(FText::Format(
			NSLOCTEXT("Save", "StoreLevelFormat", "상점 레벨 {0}"),
			FText::AsNumber(Header.StoreLevel)));
	}

	if (IsValid(PlayTimeText))
	{
		PlayTimeText->SetText(MakePlayTimeText(Header.PlayTimeSeconds));
	}

	if (IsValid(SavedAtText))
	{
		// UTC로 보관한 저장 시각을 지역 시간으로 변환해 표시
		const FTimespan LocalOffset = FDateTime::Now() - FDateTime::UtcNow();
		SavedAtText->SetText(FText::AsDateTime(Header.SavedAtUtc + LocalOffset));
	}

	if (IsValid(LoadButton)) { LoadButton->SetIsEnabled(true); }
	if (IsValid(DeleteButton)) { DeleteButton->SetIsEnabled(true); }
}

FText UCSSaveSlotWidget::MakePlayTimeText(float InPlayTimeSeconds)
{
	const int32 TotalMinutes = FMath::Max(0, FMath::FloorToInt(InPlayTimeSeconds / 60.0f));
	const int32 Hours = TotalMinutes / 60;
	const int32 Minutes = TotalMinutes % 60;
	return FText::Format(
		NSLOCTEXT("Save", "PlayTimeFormat", "플레이 {0}시간 {1}분"),
		FText::AsNumber(Hours),
		FText::AsNumber(Minutes));
}

void UCSSaveSlotWidget::HandleSaveClicked()
{
	OnSaveRequested.Broadcast(SlotName);
}

void UCSSaveSlotWidget::HandleLoadClicked()
{
	if (bIsEmptySlot)
	{
		return;
	}

	OnLoadRequested.Broadcast(SlotName);
}

void UCSSaveSlotWidget::HandleDeleteClicked()
{
	if (bIsEmptySlot)
	{
		return;
	}

	OnDeleteRequested.Broadcast(SlotName);
}
