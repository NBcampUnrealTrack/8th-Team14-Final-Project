#include "UI/Shelf/CSShelfConfigurationWidget.h"

#include "Features/Shelves/CSShelfActor.h"

void UCShelfConfigurationWidget::SetShelfTarget(ACSShelfActor* InShelf)
{
	ShelfTarget = InShelf;
	// 위젯이 열리거나 설정이 바뀔 때 BP가 최신 단별 정보를 다시 표시하도록 알립니다.
	RefreshConfigurationView();
}

bool UCShelfConfigurationWidget::ConfigureLevelProduct(
	ECSShelfSlotLevel SlotLevel,
	FDataTableRowHandle ProductRow)
{
	ACSShelfActor* Shelf = ShelfTarget.Get();
	if (!IsValid(Shelf) || !Shelf->SetLevelProductConfiguration(SlotLevel, ProductRow))
	{
		LastConfigurationStatus = FText::FromString(TEXT("이 상품으로 진열 구성을 변경할 수 없습니다."));
		ShowConfigurationStatus(LastConfigurationStatus, 0);
		return false;
	}

	const int32 PendingReorganizationCount = Shelf->GetMismatchedProductCountOnLevel(SlotLevel);
	LastConfigurationStatus = PendingReorganizationCount > 0
		? FText::Format(
			NSLOCTEXT("ShelfConfiguration", "ConfigurationChangedPendingReorganization",
				"기존 상품 {0}개를 직원이 정리한 뒤\n새 상품으로 변경됩니다."),
			FText::AsNumber(PendingReorganizationCount))
		: FText::FromString(TEXT("진열 상품을 변경했습니다."));
	ShowConfigurationStatus(LastConfigurationStatus, PendingReorganizationCount);
	RefreshConfigurationView();
	return true;
}

bool UCShelfConfigurationWidget::ClearLevelProduct(ECSShelfSlotLevel SlotLevel)
{
	ACSShelfActor* Shelf = ShelfTarget.Get();
	if (!IsValid(Shelf) || !Shelf->ClearLevelProductConfiguration(SlotLevel))
	{
		LastConfigurationStatus = FText::FromString(TEXT("진열 구성 해제에 실패했습니다."));
		ShowConfigurationStatus(LastConfigurationStatus, 0);
		return false;
	}

	LastConfigurationStatus = FText::FromString(TEXT("진열 구성을 자동 배치로 되돌렸습니다."));
	ShowConfigurationStatus(LastConfigurationStatus, 0);
	RefreshConfigurationView();
	return true;
}

void UCShelfConfigurationWidget::CloseConfiguration()
{
	OnConfigurationClosed.Broadcast();
	RemoveFromParent();
}
