#include "UI/Ordering/CSWorldOrderWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Components/ScrollBox.h"
#include "Features/Ordering/CSOrderWorldWidgetComponent.h"
#include "Systems/Ordering/CSOrderSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Engine/World.h"

namespace
{
bool SameOrderRow(const FDataTableRowHandle& A, const FDataTableRowHandle& B)
{
	return A.DataTable == B.DataTable && A.RowName == B.RowName;
}
}

int32 UCSWorldOrderWidget::GetNativeOrderQuantity(const FDataTableRowHandle& Row, bool bFacility) const
{
	if (bFacility)
	{
		for (const auto& Line : NativeFacilityLines)
			if (SameOrderRow(Line.FacilityRow, Row)) return Line.Quantity;
	}
	else
	{
		for (const auto& Line : NativeProductLines)
			if (SameOrderRow(Line.ProductRow, Row)) return Line.CrateCount;
	}
	return 0;
}

void UCSWorldOrderWidget::SetNativeOrderQuantity(const FDataTableRowHandle& Row, bool bFacility, int32 Quantity)
{
	if (bSubmittingNativeOrder || Row.IsNull()) return;
	Quantity = FMath::Max(0, Quantity);
	if (bFacility)
	{
		NativeFacilityLines.RemoveAll([&](const FCSFacilityOrderLine& Line) { return SameOrderRow(Line.FacilityRow, Row); });
		if (Quantity > 0)
		{
			auto& Line = NativeFacilityLines.AddDefaulted_GetRef();
			Line.FacilityRow = Row;
			Line.Quantity = Quantity;
		}
	}
	else
	{
		NativeProductLines.RemoveAll([&](const FCSOrderLine& Line) { return SameOrderRow(Line.ProductRow, Row); });
		if (Quantity > 0)
		{
			auto& Line = NativeProductLines.AddDefaulted_GetRef();
			Line.ProductRow = Row;
			Line.CrateCount = Quantity;
		}
	}
}

void UCSWorldOrderWidget::ResetNativeOrderCart()
{
	if (bSubmittingNativeOrder) return;
	NativeProductLines.Reset();
	NativeFacilityLines.Reset();
}

FCSCombinedOrderResult UCSWorldOrderWidget::EvaluateNativeOrderCart(ECSOrderDeliveryType DeliveryType) const
{
	if (const UCSOrderSubsystem* Orders = GetWorld() ? GetWorld()->GetSubsystem<UCSOrderSubsystem>() : nullptr)
		return Orders->EvaluateCombinedOrder(NativeProductLines, NativeFacilityLines, DeliveryType);
	FCSCombinedOrderResult Result;
	Result.DeliveryType = DeliveryType;
	Result.Failure = ECSCombinedOrderFailure::Unavailable;
	Result.Message = NSLOCTEXT("Order", "Unavailable", "발주 정보를 불러올 수 없습니다.");
	return Result;
}

FCSCombinedOrderResult UCSWorldOrderWidget::SubmitNativeOrderCart(ECSOrderDeliveryType DeliveryType)
{
	if (bSubmittingNativeOrder || bOpening || bClosing)
	{
		FCSCombinedOrderResult Result;
		Result.Failure = ECSCombinedOrderFailure::Busy;
		Result.Message = NSLOCTEXT("Order", "Busy", "발주 처리 중입니다.");
		return Result;
	}
	UCSOrderSubsystem* Orders = GetWorld() ? GetWorld()->GetSubsystem<UCSOrderSubsystem>() : nullptr;
	if (!Orders) return EvaluateNativeOrderCart(DeliveryType);
	FCSCombinedOrderResult Result;
	{
		TGuardValue<bool> Guard(bSubmittingNativeOrder, true);
		if (UWidget* Button = GetWidgetFromName(TEXT("Btn_Order"))) Button->SetIsEnabled(false);
		// Keep inputs stable even if a funds/delivery listener refreshes the UI during checkout.
		const auto Products = NativeProductLines;
		const auto Facilities = NativeFacilityLines;
		Result = Orders->PlaceCombinedOrder(Products, Facilities, DeliveryType);
		if (Result.bSuccess)
		{
			NativeProductLines.Reset();
			NativeFacilityLines.Reset();
		}
	}
	RefreshNativeOrderScreen();
	if (UWidget* Button = GetWidgetFromName(TEXT("Btn_Order"))) Button->SetIsEnabled(true);
	return Result;
}

void UCSWorldOrderWidget::OpenInWorld(UCSOrderWorldWidgetComponent* InHost)
{
	WorldHost = InHost;
	bClosing = false;
	bCompletingClose = false;
	bOpening = true;
	// Every new opening starts at the first unlocked product, independent of
	// the tab and detail selection used during the previous session.
	bNativeFacilityTab = false;
	NativeSelectedProduct = FDataTableRowHandle();
	NativeSelectedFacility = FDataTableRowHandle();
	HideNativeNotice();
	RefreshNativeOrderScreen();
	if (UScrollBox* Scroll = Cast<UScrollBox>(GetWidgetFromName(TEXT("Scroll_ListViewport"))))
		Scroll->ScrollToStart();
	SetVisibility(ESlateVisibility::Visible);
	SetIsEnabled(false);
	if (Anim_OpenOrder) PlayOrderTransition(false);
	else
	{
		bOpening = false;
		SetIsEnabled(true);
		SetKeyboardFocus();
	}
}

void UCSWorldOrderWidget::RequestNativeOrderClose()
{
	if (bSubmittingNativeOrder || bOpening || bClosing) return;
	if (!DeferCloseForWorldAnimation()) CompleteCloseAfterCameraReturn();
}

bool UCSWorldOrderWidget::DeferCloseForWorldAnimation()
{
	if (bCompletingClose)
	{
		return false;
	}
	if (bSubmittingNativeOrder) return true;
	if (!WorldHost)
	{
		CompleteCloseAfterCameraReturn();
		return true;
	}
	if (bOpening || bClosing)
	{
		return true;
	}
	bClosing = true;
	SetIsEnabled(false);
	HideNativeNotice();
	if (Anim_OpenOrder) PlayOrderTransition(true);
	else WorldHost->FinishPresentation();
	return true;
}

void UCSWorldOrderWidget::PlayOrderTransition_Implementation(bool bReverse)
{
	if (bReverse) PlayAnimationReverse(Anim_OpenOrder);
	else PlayAnimation(Anim_OpenOrder);
}

void UCSWorldOrderWidget::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{
	Super::OnAnimationFinished_Implementation(Animation);
	if (Animation != Anim_OpenOrder)
	{
		return;
	}
	if (bClosing)
	{
		if (WorldHost)
		{
			WorldHost->FinishPresentation();
		}
		else
		{
			CompleteCloseAfterCameraReturn();
		}
	}
	else if (bOpening)
	{
		bOpening = false;
		SetIsEnabled(true);
		SetKeyboardFocus();
	}
}

void UCSWorldOrderWidget::CompleteCloseAfterCameraReturn()
{
	bCompletingClose = true;
	ResetNativeOrderCart();
	HideNativeNotice();
	SetVisibility(ESlateVisibility::Collapsed);
	// Use the shared lock API so tutorial observers and other UI owners retain
	// their existing semantics. Only this widget's lock is released.
	if (ACSStorePlayerController* Player = Cast<ACSStorePlayerController>(GetOwningPlayer()))
		Player->SetGameplayUIInputLock(this, false);
	bOpening = false;
	bClosing = false;
	bCompletingClose = false;
	WorldHost = nullptr;
	RemoveFromParent();
}

void UCSWorldOrderWidget::NativeDestruct()
{
	HideNativeNotice();
	if (NativeObservedState)
		NativeObservedState->OnFundsChanged.RemoveDynamic(this, &ThisClass::NativeFundsChanged);
	NativeObservedState = nullptr;
	if (WorldHost)
	{
		WorldHost->CancelPresentation();
	}
	Super::NativeDestruct();
}
