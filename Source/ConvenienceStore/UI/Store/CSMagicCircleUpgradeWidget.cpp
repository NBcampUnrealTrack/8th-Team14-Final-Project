#include "UI/Store/CSMagicCircleUpgradeWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

UCSMagicCircleUpgradeWidget::UCSMagicCircleUpgradeWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UCSMagicCircleUpgradeWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshView();
}

void UCSMagicCircleUpgradeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	StoreState = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConfirmClicked);
	if (IsValid(CancelButton)) CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Close);
	if (IsValid(StoreState))
	{
		StoreState->OnStoreLevelChanged.AddUniqueDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnFundsChanged.AddUniqueDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnMagicCircleUpgraded.AddUniqueDynamic(this, &ThisClass::HandleUpgraded);
		StoreState->OnDailySummaryRequested.AddUniqueDynamic(this, &ThisClass::Close);
	}
	if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(GetOwningPlayer())) Controller->SetGameplayUIInputLock(this, true);
	RefreshView();
}

void UCSMagicCircleUpgradeWidget::NativeDestruct()
{
	if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConfirmClicked);
	if (IsValid(CancelButton)) CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::Close);
	if (IsValid(StoreState))
	{
		StoreState->OnStoreLevelChanged.RemoveDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnFundsChanged.RemoveDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnMagicCircleUpgraded.RemoveDynamic(this, &ThisClass::HandleUpgraded);
		StoreState->OnDailySummaryRequested.RemoveDynamic(this, &ThisClass::Close);
	}
	StoreState = nullptr;
	if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(GetOwningPlayer())) Controller->SetGameplayUIInputLock(this, false);
	Super::NativeDestruct();
}

void UCSMagicCircleUpgradeWidget::RefreshView()
{
	if (IsDesignTime()) ViewData = PreviewData;
	else
	{
		ViewData = FCSMagicCircleUpgradeViewData();
		ViewData.CurrentLevel = ViewData.NextLevel = ViewData.DailyOrderLimit = ViewData.NextDailyOrderLimit = 0;
		ViewData.UpgradeCost = 0;
		ViewData.bCanUpgrade = false;
		const ACSStoreGameState* State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
		if (IsValid(State))
		{
			ViewData.CurrentLevel = State->GetMagicCircleLevel();
			ViewData.bMaximumLevel = ViewData.CurrentLevel >= 5;
			ViewData.NextLevel = FMath::Min(5, ViewData.CurrentLevel + 1);
			ViewData.DailyOrderLimit = ViewData.CurrentLevel;
			ViewData.NextDailyOrderLimit = ViewData.NextLevel;
			ViewData.RequiredStoreLevel = State->GetMagicCircleRequiredStoreLevel();
			ViewData.CurrentStoreLevel = State->GetStoreLevel();
			ViewData.UpgradeCost = State->GetMagicCircleUpgradeCost();
			ViewData.bCanUpgrade = State->CanUpgradeMagicCircle(ViewData.Message);
			if (!ActionMessage.IsEmpty() && !ViewData.bMaximumLevel) ViewData.Message = ActionMessage;
		}
		else ViewData.Message = NSLOCTEXT("MagicCircle", "MissingState", "매장 정보를 찾을 수 없습니다.");
	}
	if (IsValid(CurrentLevelText)) CurrentLevelText->SetText(FText::Format(
		NSLOCTEXT("MagicCircle", "CurrentLevel", "{0}단계"), FText::AsNumber(ViewData.CurrentLevel)));
	if (IsValid(NextLevelText)) NextLevelText->SetText(FText::Format(
		NSLOCTEXT("MagicCircle", "NextLevel", "{0}단계"), FText::AsNumber(ViewData.NextLevel)));
	if (IsValid(CurrentDailyOrderLimitText)) CurrentDailyOrderLimitText->SetText(FText::Format(
		NSLOCTEXT("MagicCircle", "CurrentDailyOrderLimit", "{0}건"), FText::AsNumber(ViewData.DailyOrderLimit)));
	if (IsValid(NextDailyOrderLimitText)) NextDailyOrderLimitText->SetText(FText::Format(
		NSLOCTEXT("MagicCircle", "NextDailyOrderLimit", "{0}건"), FText::AsNumber(ViewData.NextDailyOrderLimit)));
	if (IsValid(UpgradeCostText)) UpgradeCostText->SetText(FText::AsNumber(ViewData.UpgradeCost));
	if (IsValid(AvailabilityText)) AvailabilityText->SetText(ViewData.Message);
	if (IsValid(ConfirmButton)) ConfirmButton->SetIsEnabled(ViewData.bCanUpgrade && !ViewData.bMaximumLevel);
	if (IsValid(ConfirmButtonText)) ConfirmButtonText->SetText(ViewData.bMaximumLevel ? NSLOCTEXT("MagicCircle", "Maximum", "최대 단계 달성") : NSLOCTEXT("MagicCircle", "Confirm", "강화하기"));
	OnUpgradeViewUpdated();
}

bool UCSMagicCircleUpgradeWidget::ConfirmUpgrade()
{
	if (IsDesignTime()) return false;
	ACSStoreGameState* State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	FText Result;
	const bool bSucceeded = IsValid(State) && State->TryUpgradeMagicCircle(Result);
	ActionMessage = Result;
	RefreshView();
	if (bSucceeded) OnUpgradeSucceeded(State->GetMagicCircleLevel());
	return bSucceeded;
}

void UCSMagicCircleUpgradeWidget::Close() { RemoveFromParent(); }
void UCSMagicCircleUpgradeWidget::HandleConfirmClicked() { ConfirmUpgrade(); }
void UCSMagicCircleUpgradeWidget::HandleFundsChanged(int32 NewFunds) { ActionMessage = FText::GetEmpty(); RefreshView(); }
void UCSMagicCircleUpgradeWidget::HandleUpgraded(int32 NewLevel) { RefreshView(); }

FReply UCSMagicCircleUpgradeWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape) { Close(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}
