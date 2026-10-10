#include "UI/Store/CSEmployeeHiringWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "Systems/Employees/CSEmployeeHiringSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

UCSEmployeeHiringWidget::UCSEmployeeHiringWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	PreviewData.RoleName = NSLOCTEXT("EmployeeHiring", "Checkout", "계산 직원");
	PreviewData.Duties = NSLOCTEXT("EmployeeHiring", "CheckoutDuties", "손님 계산 및 상품 스캔");
}

void UCSEmployeeHiringWidget::SetApplicant(ACSStoreEmployeeCharacter* InApplicant)
{
	Applicant = InApplicant;
	PaymentError = FText::GetEmpty();
	if (bViewConstructed) RefreshView();
}

void UCSEmployeeHiringWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshView();
}

void UCSEmployeeHiringWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bViewConstructed = true;
	StoreState = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	HiringSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>() : nullptr;
	if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConfirmClicked);
	if (IsValid(CancelButton)) CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Close);
	if (IsValid(StoreState))
	{
		StoreState->OnStoreLevelChanged.AddUniqueDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnFundsChanged.AddUniqueDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnStorePhaseChanged.AddUniqueDynamic(this, &ThisClass::HandlePhaseChanged);
		StoreState->OnBusinessDayStarted.AddUniqueDynamic(this, &ThisClass::HandleBusinessDayChanged);
		StoreState->OnDailySummaryRequested.AddUniqueDynamic(this, &ThisClass::Close);
	}
	if (IsValid(HiringSubsystem)) HiringSubsystem->OnEmployeeHired.AddUniqueDynamic(this, &ThisClass::HandleEmployeeHired);
	if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(GetOwningPlayer())) Controller->SetGameplayUIInputLock(this, true);
	RefreshView();
}

void UCSEmployeeHiringWidget::NativeDestruct()
{
	bViewConstructed = false;
	if (IsValid(ConfirmButton)) ConfirmButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConfirmClicked);
	if (IsValid(CancelButton)) CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::Close);
	if (IsValid(StoreState))
	{
		StoreState->OnStoreLevelChanged.RemoveDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnFundsChanged.RemoveDynamic(this, &ThisClass::HandleFundsChanged);
		StoreState->OnStorePhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhaseChanged);
		StoreState->OnBusinessDayStarted.RemoveDynamic(this, &ThisClass::HandleBusinessDayChanged);
		StoreState->OnDailySummaryRequested.RemoveDynamic(this, &ThisClass::Close);
	}
	if (IsValid(HiringSubsystem)) HiringSubsystem->OnEmployeeHired.RemoveDynamic(this, &ThisClass::HandleEmployeeHired);
	StoreState = nullptr;
	HiringSubsystem = nullptr;
	if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(GetOwningPlayer())) Controller->SetGameplayUIInputLock(this, false);
	Super::NativeDestruct();
}

void UCSEmployeeHiringWidget::Close() { RemoveFromParent(); }

FReply UCSEmployeeHiringWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

void UCSEmployeeHiringWidget::RefreshView()
{
	if (IsDesignTime()) ViewData = PreviewData;
	else
	{
		ViewData = FCSEmployeeHiringViewData();
		ViewData.HireCost = ViewData.DailyWage = ViewData.TotalDailyWage = ViewData.CurrentFunds = ViewData.StartBusinessDay = 0;
		ViewData.bCanHire = false;
		const ACSStoreEmployeeCharacter* Employee = Applicant.Get();
		const ACSStoreGameState* State = GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
		const UCSEmployeeHiringSubsystem* Hiring = GetWorld() ? GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>() : nullptr;
		if (IsValid(Employee) && IsValid(State) && IsValid(Hiring))
		{
			const ECSEmployeeHireSlot HireSlot = Employee->GetHiringSlot();
			ViewData.RoleName = Employee->GetHiringRoleName();
			ViewData.Duties = HireSlot == ECSEmployeeHireSlot::Checkout
				? NSLOCTEXT("EmployeeHiring", "CheckoutDuties", "손님 계산 및 상품 스캔")
				: NSLOCTEXT("EmployeeHiring", "RestockDuties", "상품 진열 · 재고 정리 · 신선도 관리");
			ViewData.RequiredStoreLevel = State->GetEmployeeRequiredStoreLevel(HireSlot);
			ViewData.CurrentStoreLevel = State->GetStoreLevel();
			ViewData.HireCost = State->GetEmployeeHireCost(HireSlot);
			ViewData.DailyWage = State->GetEmployeeDailyWage(HireSlot);
			ViewData.TotalDailyWage = Hiring->GetTotalDailyWage() + (Hiring->IsSlotHired(HireSlot) ? 0 : ViewData.DailyWage);
			ViewData.CurrentFunds = State->GetCurrentFunds();
			ViewData.StartBusinessDay = Hiring->GetNextStartBusinessDay();
			ViewData.bStartsToday = ViewData.StartBusinessDay == State->CurrentBusinessDay;
			ViewData.bCanHire = Hiring->CanHire(Employee, ViewData.UnavailableReason);
			if (ViewData.bCanHire) ViewData.UnavailableReason = PaymentError;
		}
		else ViewData.UnavailableReason = NSLOCTEXT("EmployeeHiring", "Gone", "지원자 또는 매장 정보를 찾을 수 없습니다.");
	}
	if (IsValid(RoleNameText)) RoleNameText->SetText(ViewData.RoleName);
	if (IsValid(DutiesText)) DutiesText->SetText(ViewData.Duties);
	const auto Gold = [](int32 Amount) { return FText::Format(NSLOCTEXT("EmployeeHiring", "Gold", "{0}G"), FText::AsNumber(Amount)); };
	if (IsValid(HireCostText)) HireCostText->SetText(FText::AsNumber(ViewData.HireCost));
	if (IsValid(RequiredStoreLevelText)) RequiredStoreLevelText->SetText(FText::Format(
		NSLOCTEXT("EmployeeHiring", "RequiredStoreLevel", "가게 레벨 {0} 필요 (현재 {1})"),
		FText::AsNumber(ViewData.RequiredStoreLevel), FText::AsNumber(ViewData.CurrentStoreLevel)));
	if (IsValid(DailyWageText)) DailyWageText->SetText(FText::AsNumber(ViewData.DailyWage));
	if (IsValid(TotalDailyWageText)) TotalDailyWageText->SetText(FText::AsNumber(ViewData.TotalDailyWage));
	if (IsValid(CurrentFundsText)) CurrentFundsText->SetText(Gold(ViewData.CurrentFunds));
	if (IsValid(StartDayText)) StartDayText->SetText(ViewData.StartBusinessDay > 0
		? FText::Format(ViewData.bStartsToday
			? NSLOCTEXT("EmployeeHiring", "StartToday", "오늘 ({0}일차)")
			: NSLOCTEXT("EmployeeHiring", "StartTomorrow", "내일 ({0}일차)"), FText::AsNumber(ViewData.StartBusinessDay))
		: FText::GetEmpty());
	if (IsValid(WagePaymentNoticeText)) WagePaymentNoticeText->SetText(
		NSLOCTEXT("EmployeeHiring", "WagePaymentNotice", "일급은 매일 영업 종료 시 자동 지급됩니다."));
	if (IsValid(AvailabilityText)) AvailabilityText->SetText(ViewData.UnavailableReason);
	if (IsValid(ConfirmButton)) ConfirmButton->SetIsEnabled(ViewData.bCanHire);
	OnHiringViewUpdated();
}

bool UCSEmployeeHiringWidget::ConfirmHire()
{
	if (IsDesignTime()) return false;
	UCSEmployeeHiringSubsystem* Hiring = GetWorld() ? GetWorld()->GetSubsystem<UCSEmployeeHiringSubsystem>() : nullptr;
	if (Hiring && Hiring->TryHire(Applicant.Get(), PaymentError))
	{
		Close();
		return true;
	}
	RefreshView();
	return false;
}

void UCSEmployeeHiringWidget::HandleConfirmClicked() { ConfirmHire(); }
void UCSEmployeeHiringWidget::HandleFundsChanged(int32 NewFunds) { PaymentError = FText::GetEmpty(); RefreshView(); }
void UCSEmployeeHiringWidget::HandlePhaseChanged(ECStorePhase NewPhase) { RefreshView(); }
void UCSEmployeeHiringWidget::HandleBusinessDayChanged(int32 NewDay) { RefreshView(); }
void UCSEmployeeHiringWidget::HandleEmployeeHired() { RefreshView(); }
