#include "UI/Shelf/CSShelfConfigurationWidget.h"

#include "Features/Shelves/CSShelfActor.h"
#include "Systems/Employees/CSEmployeeHiringSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

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
	// 지정 상품은 진열 직원만 채워 줘서 직원이 없으면 지정해도 진열대가 그대로 빈다
	const UWorld* World = GetWorld();
	const UCSEmployeeHiringSubsystem* Hiring = World ? World->GetSubsystem<UCSEmployeeHiringSubsystem>() : nullptr;
	if (IsValid(Hiring) &&
		!Hiring->IsSlotHired(ECSEmployeeHireSlot::FirstRestock) &&
		!Hiring->IsSlotHired(ECSEmployeeHireSlot::SecondRestock))
	{
		if (ACSStorePlayerController* StorePlayerController = Cast<ACSStorePlayerController>(GetOwningPlayer()))
		{
			StorePlayerController->ShowNoticeBanner(NSLOCTEXT("ShelfConfiguration", "RestockEmployeeRequired",
				"진열 직원을 고용해야 선택할 수 있습니다."));
		}
		return false;
	}

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
