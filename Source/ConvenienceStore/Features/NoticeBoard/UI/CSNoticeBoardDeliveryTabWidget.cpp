#include "Features/NoticeBoard/UI/CSNoticeBoardDeliveryTabWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CSProductTypes.h"
#include "Features/NoticeBoard/UI/CSNoticeBoardWidget.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"

void UCSNoticeBoardDeliveryTabWidget::NativeConstruct()
{
	Super::NativeConstruct();

	DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	if (IsValid(DeliveryOrderSubsystem))
	{
		DeliveryOrderSubsystem->OnDeliveryOrdersChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleDeliveryOrdersChanged);
	}
	if (IsValid(Button_Accept))
	{
		Button_Accept->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleAcceptClicked);
	}
	if (IsValid(Button_Decline))
	{
		Button_Decline->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDeclineClicked);
	}
	RefreshDeliveryOrderList();
}

void UCSNoticeBoardDeliveryTabWidget::NativeDestruct()
{
	if (IsValid(Button_Accept))
	{
		Button_Accept->OnClicked.RemoveDynamic(this, &ThisClass::HandleAcceptClicked);
	}
	if (IsValid(Button_Decline))
	{
		Button_Decline->OnClicked.RemoveDynamic(this, &ThisClass::HandleDeclineClicked);
	}
	if (IsValid(DeliveryOrderSubsystem))
	{
		DeliveryOrderSubsystem->OnDeliveryOrdersChanged.RemoveDynamic(
			this,
			&ThisClass::HandleDeliveryOrdersChanged);
	}
	DeliveryOrderSubsystem = nullptr;
	Super::NativeDestruct();
}

void UCSNoticeBoardDeliveryTabWidget::RefreshDeliveryOrderList()
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		RefreshSingleOrderPresentation(FCSDeliveryOrderRuntime(), false, FText::GetEmpty());
		RefreshDeliveryOrderListView(TArray<FCSDeliveryOrderRuntime>(), 0, 0);
		RefreshRecentDeliveryResults(TArray<FCSDeliveryOrderRuntime>());
		RefreshDeliveryOrderView(FCSDeliveryOrderRuntime(), false, FText::GetEmpty());
		return;
	}

	FText AvailabilityMessage;
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	const bool bCanAccept = DisplayedOrder.OrderId != INDEX_NONE &&
		DeliveryOrderSubsystem->CanAcceptOrder(DisplayedOrder.OrderId, AvailabilityMessage);
	RefreshSingleOrderPresentation(DisplayedOrder, bCanAccept, AvailabilityMessage);
	RefreshDeliveryOrderView(DisplayedOrder, bCanAccept, AvailabilityMessage);
	const TArray<FCSDeliveryOrderRuntime> Orders = DeliveryOrderSubsystem->GetDeliveryOrders();
	const int32 AcceptedOrderCount = DeliveryOrderSubsystem->GetAcceptedOrderCount();
	SetNamedText(TEXT("Text_OrderSummary"), FText::Format(
		NSLOCTEXT("NoticeBoard", "DeliveryOrderSummary", "현재 주문 {0}건 · 진행 중 {1}건"),
		FText::AsNumber(Orders.Num()),
		FText::AsNumber(AcceptedOrderCount)));
	SetNamedText(TEXT("Text_CurrentOrderCount"), FText::AsNumber(Orders.Num()));
	SetNamedText(TEXT("Text_InProgressOrderCount"), FText::AsNumber(AcceptedOrderCount));
	RefreshDeliveryOrderListView(
		Orders,
		DeliveryOrderSubsystem->GetTodayGeneratedOrderCount(),
		AcceptedOrderCount);
	RefreshRecentDeliveryResults(DeliveryOrderSubsystem->GetRecentDeliveryResults());
}

void UCSNoticeBoardDeliveryTabWidget::RefreshSingleOrderPresentation(
	const FCSDeliveryOrderRuntime& Order,
	const bool bCanAccept,
	const FText& AvailabilityMessage)
{
	const bool bHasOrder = Order.OrderId != INDEX_NONE;
	SetNamedVisibility(TEXT("Overlay_EmptyState"),
		bHasOrder ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	SetNamedVisibility(TEXT("Overlay_OrderContent"),
		bHasOrder ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

	if (!bHasOrder)
	{
		if (UPanelWidget* ItemPanel = ResolveRequestItemPanel())
		{
			ItemPanel->ClearChildren();
		}
		SetNamedText(TEXT("Text_EmptyMessage"),
			NSLOCTEXT("NoticeBoard", "NoDeliveryOrder", "현재 도착한 배달 주문이 없습니다."));
		if (IsValid(Button_Accept)) Button_Accept->SetIsEnabled(false);
		if (IsValid(Button_Decline)) Button_Decline->SetIsEnabled(false);
		return;
	}

	SetNamedText(TEXT("Text_RecipientName"), Order.RecipientDisplayName);
	SetNamedText(TEXT("Text_RecipientRegion"), Order.RecipientRegionName);
	const FText AcceptanceDeadlineText = FormatGameClock(Order.AcceptanceDeadlineMinutes);
	SetNamedText(TEXT("Text_AcceptanceDeadline"), AcceptanceDeadlineText);
	SetNamedText(TEXT("Text_AcceptanceDeadlineDetail"), AcceptanceDeadlineText);
	SetNamedText(TEXT("Text_DeliveryDeadline"),
		Order.Status == ECSDeliveryOrderStatus::Accepted
			? FormatGameClock(Order.DeliveryDeadlineMinutes)
			: NSLOCTEXT("NoticeBoard", "DeliveryAfterAccept", "수락 후 3시간"));
	SetNamedText(TEXT("Text_RewardGold"), FText::AsNumber(Order.RewardGold));
	SetNamedText(TEXT("Text_ReputationReward"), FText::Format(
		NSLOCTEXT("NoticeBoard", "ReputationReward", "+{0}"),
		FText::AsNumber(Order.ReputationReward)));

	const bool bPending = Order.Status == ECSDeliveryOrderStatus::PendingAcceptance;
	const bool bAccepted = Order.Status == ECSDeliveryOrderStatus::Accepted;
	SetNamedVisibility(TEXT("Overlay_StockReady"),
		bPending && bCanAccept
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	SetNamedVisibility(TEXT("Overlay_StockShortage"),
		bPending && !bCanAccept
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	SetNamedVisibility(TEXT("Overlay_DeliveryInProgress"),
		bAccepted
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);

	const auto SetActionVisibility = [](UButton* Button, const bool bVisible)
	{
		if (!IsValid(Button))
		{
			return;
		}

		const ESlateVisibility Visibility = bVisible
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed;
		if (UPanelWidget* ActionContainer = Button->GetParent())
		{
			ActionContainer->SetVisibility(Visibility);
		}
		Button->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	};
	SetActionVisibility(Button_Accept, bPending);
	SetActionVisibility(Button_Decline, bPending);

	if (IsValid(Button_Accept))
	{
		Button_Accept->SetIsEnabled(bPending && bCanAccept);
	}
	if (IsValid(Button_Decline))
	{
		Button_Decline->SetIsEnabled(bPending);
	}

	SetNamedText(TEXT("Text_StockState"), bAccepted
		? NSLOCTEXT("NoticeBoard", "DeliveryInProgress", "배달 진행 중")
		: (bCanAccept
			? NSLOCTEXT("NoticeBoard", "DeliveryStockReady", "수락 가능")
			: NSLOCTEXT("NoticeBoard", "DeliveryStockShortage", "상품이 부족합니다")));
	SetNamedText(TEXT("Text_AvailabilityMessage"), bAccepted
		? NSLOCTEXT("NoticeBoard", "DeliveryInProgressDescription", "주문 상품을 마법진에 올려 발송하세요.")
		: (bCanAccept
			? NSLOCTEXT("NoticeBoard", "DeliveryReadyDescription", "모든 요청 상품이 준비되었습니다.")
			: (AvailabilityMessage.IsEmpty()
				? NSLOCTEXT("NoticeBoard", "DeliveryShortageDescription", "필요한 상품을 준비한 후 수락할 수 있습니다.")
				: AvailabilityMessage)));

	PopulateRequestItemRows(Order);
}

void UCSNoticeBoardDeliveryTabWidget::PopulateRequestItemRows(
	const FCSDeliveryOrderRuntime& Order)
{
	UPanelWidget* ItemPanel = ResolveRequestItemPanel();
	const TSubclassOf<UUserWidget> RowClass = ResolveRequestItemRowClass();
	if (!IsValid(ItemPanel))
	{
		return;
	}

	ItemPanel->ClearChildren();
	if (!RowClass || !IsValid(DeliveryOrderSubsystem))
	{
		return;
	}

	for (const FCSDeliveryOrderLine& Line : Order.RequiredProducts)
	{
		UUserWidget* RowWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), RowClass);
		if (!IsValid(RowWidget))
		{
			continue;
		}

		int32 AvailableQuantity = DeliveryOrderSubsystem->GetAvailableQuantityForOrder(Line.ProductRow);
		if (Order.Status == ECSDeliveryOrderStatus::Accepted)
		{
			if (const UCSStoreStockSubsystem* StockSubsystem =
				GetWorld() != nullptr ? GetWorld()->GetSubsystem<UCSStoreStockSubsystem>() : nullptr)
			{
				AvailableQuantity = StockSubsystem->GetProductStock(Line.ProductRow.RowName).TotalQuantity;
			}
		}
		SetupRequestItemRow(RowWidget, Line, AvailableQuantity);
		ItemPanel->AddChild(RowWidget);
	}
}

void UCSNoticeBoardDeliveryTabWidget::SetupRequestItemRow(
	UUserWidget* RowWidget,
	const FCSDeliveryOrderLine& Line,
	const int32 AvailableQuantity) const
{
	const FCSProductDataRow* Product = Line.ProductRow.GetRow<FCSProductDataRow>(
		TEXT("UCSNoticeBoardDeliveryTabWidget::SetupRequestItemRow"));
	const FText ProductName = Product != nullptr && !Product->DisplayName.IsEmpty()
		? Product->DisplayName
		: FText::FromName(Line.ProductRow.RowName);
	const bool bSufficient = AvailableQuantity >= Line.Quantity;

	if (UTextBlock* Text = Cast<UTextBlock>(RowWidget->GetWidgetFromName(TEXT("Text_ProductName"))))
		Text->SetText(ProductName);
	if (UTextBlock* Text = Cast<UTextBlock>(RowWidget->GetWidgetFromName(TEXT("Text_RequiredQuantity"))))
		Text->SetText(FText::Format(NSLOCTEXT("NoticeBoard", "DeliveryQuantity", "{0}개"), FText::AsNumber(Line.Quantity)));
	if (UTextBlock* Text = Cast<UTextBlock>(RowWidget->GetWidgetFromName(TEXT("Text_CurrentStock"))))
		Text->SetText(FText::Format(NSLOCTEXT("NoticeBoard", "DeliveryQuantity", "{0}개"), FText::AsNumber(AvailableQuantity)));
	if (UTextBlock* Text = Cast<UTextBlock>(RowWidget->GetWidgetFromName(TEXT("Text_ItemStatus"))))
		Text->SetText(bSufficient
			? NSLOCTEXT("NoticeBoard", "DeliveryItemEnough", "충족")
			: NSLOCTEXT("NoticeBoard", "DeliveryItemShort", "부족"));
	if (UWidget* EnoughImage = RowWidget->GetWidgetFromName(TEXT("Image_ItemStatusEnough")))
	{
		EnoughImage->SetVisibility(
			bSufficient ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (UWidget* ShortageImage = RowWidget->GetWidgetFromName(TEXT("Image_ItemStatusShortage")))
	{
		ShortageImage->SetVisibility(
			bSufficient ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	// 상품 이미지는 의도적으로 조회하거나 생성하지 않습니다.
}

UPanelWidget* UCSNoticeBoardDeliveryTabWidget::ResolveRequestItemPanel() const
{
	if (UPanelWidget* Panel = Cast<UPanelWidget>(GetWidgetFromName(TEXT("ScrollBox_RequestItems"))))
	{
		return Panel;
	}
	return Cast<UPanelWidget>(GetWidgetFromName(TEXT("VerticalBox_RequestItems")));
}

TSubclassOf<UUserWidget> UCSNoticeBoardDeliveryTabWidget::ResolveRequestItemRowClass() const
{
	if (RequestItemRowClass)
	{
		return RequestItemRowClass;
	}
	return LoadClass<UUserWidget>(
		nullptr,
		TEXT("/Game/ConvenienceStore/UI/NoticeBoard/WBP_NoticeBoardDeliveryItemRow.WBP_NoticeBoardDeliveryItemRow_C"));
}

FText UCSNoticeBoardDeliveryTabWidget::FormatGameClock(const float GameMinutes) const
{
	const int32 WholeMinutes = FMath::Max(0, FMath::FloorToInt(GameMinutes)) % 1440;
	return FText::FromString(FString::Printf(TEXT("%02d:%02d까지"), WholeMinutes / 60, WholeMinutes % 60));
}

void UCSNoticeBoardDeliveryTabWidget::SetNamedText(const FName WidgetName, const FText& Text) const
{
	if (UTextBlock* TextWidget = Cast<UTextBlock>(GetWidgetFromName(WidgetName)))
	{
		TextWidget->SetText(Text);
	}
}

void UCSNoticeBoardDeliveryTabWidget::SetNamedVisibility(
	const FName WidgetName,
	const ESlateVisibility InVisibility) const
{
	if (UWidget* Widget = GetWidgetFromName(WidgetName))
	{
		Widget->SetVisibility(InVisibility);
	}
}

void UCSNoticeBoardDeliveryTabWidget::HandleAcceptClicked()
{
	FText Message;
	AcceptDisplayedOrder(Message);
}

void UCSNoticeBoardDeliveryTabWidget::HandleDeclineClicked()
{
	FText Message;
	DeclineDisplayedOrder(Message);
}

TArray<FCSDeliveryOrderRuntime> UCSNoticeBoardDeliveryTabWidget::GetDeliveryOrders() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetDeliveryOrders()
		: TArray<FCSDeliveryOrderRuntime>();
}

TArray<FCSDeliveryOrderRuntime> UCSNoticeBoardDeliveryTabWidget::GetRecentDeliveryResults() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetRecentDeliveryResults()
		: TArray<FCSDeliveryOrderRuntime>();
}

bool UCSNoticeBoardDeliveryTabWidget::CanAcceptOrder(const int32 OrderId, FText& OutReason) const
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutReason = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystem", "배달 주문 시스템을 찾을 수 없습니다.");
		return false;
	}
	return DeliveryOrderSubsystem->CanAcceptOrder(OrderId, OutReason);
}

bool UCSNoticeBoardDeliveryTabWidget::AcceptOrder(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemAccept", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->AcceptOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	// 서브시스템이 변경 알림을 방송하지만, 실패한 요청도 버튼 상태를 즉시 재평가합니다.
	RefreshDeliveryOrderList();
	return bSucceeded;
}

bool UCSNoticeBoardDeliveryTabWidget::DeclineOrder(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemDecline", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->DeclineOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	RefreshDeliveryOrderList();
	return bSucceeded;
}

bool UCSNoticeBoardDeliveryTabWidget::CancelOrder(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemCancel", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->CancelOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	RefreshDeliveryOrderList();
	return bSucceeded;
}

bool UCSNoticeBoardDeliveryTabWidget::PrepareOrderForDispatch(const int32 OrderId, FText& OutMessage)
{
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDeliveryOrderSubsystemDispatch", "배달 주문 시스템을 찾을 수 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}

	const bool bSucceeded = DeliveryOrderSubsystem->SetActiveDispatchOrder(OrderId, OutMessage);
	ShowDeliveryOrderResult(bSucceeded, OutMessage);
	RefreshDeliveryOrderList();
	return bSucceeded;
}

FCSDeliveryOrderRuntime UCSNoticeBoardDeliveryTabWidget::GetDisplayedOrder() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetCurrentOrder()
		: FCSDeliveryOrderRuntime();
}

int32 UCSNoticeBoardDeliveryTabWidget::GetTodayGeneratedOrderCount() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetTodayGeneratedOrderCount()
		: 0;
}

int32 UCSNoticeBoardDeliveryTabWidget::GetAcceptedOrderCount() const
{
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetAcceptedOrderCount()
		: 0;
}

int32 UCSNoticeBoardDeliveryTabWidget::GetDailyOrderLimit() const
{
	return IsValid(DeliveryOrderSubsystem) ? DeliveryOrderSubsystem->GetDailyOrderLimit() : 0;
}

bool UCSNoticeBoardDeliveryTabWidget::AcceptDisplayedOrder(FText& OutMessage)
{
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	if (DisplayedOrder.OrderId == INDEX_NONE)
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDisplayedDeliveryOrder", "수락할 배달 주문이 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}
	return AcceptOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSNoticeBoardDeliveryTabWidget::DeclineDisplayedOrder(FText& OutMessage)
{
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	if (DisplayedOrder.OrderId == INDEX_NONE)
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDisplayedDeliveryOrderDecline", "거절할 배달 주문이 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}
	return DeclineOrder(DisplayedOrder.OrderId, OutMessage);
}

bool UCSNoticeBoardDeliveryTabWidget::CancelActiveOrder(FText& OutMessage)
{
	const FCSDeliveryOrderRuntime DisplayedOrder = GetDisplayedOrder();
	if (DisplayedOrder.OrderId == INDEX_NONE)
	{
		OutMessage = NSLOCTEXT("NoticeBoard", "NoDisplayedDeliveryOrderCancel", "포기할 배달 주문이 없습니다.");
		ShowDeliveryOrderResult(false, OutMessage);
		return false;
	}
	return CancelOrder(DisplayedOrder.OrderId, OutMessage);
}

void UCSNoticeBoardDeliveryTabWidget::CloseOrderWindow()
{
	if (UCSNoticeBoardWidget* NoticeBoardWidget = GetTypedOuter<UCSNoticeBoardWidget>())
	{
		NoticeBoardWidget->CloseNoticeBoardWindow();
	}
}

void UCSNoticeBoardDeliveryTabWidget::HandleDeliveryOrdersChanged()
{
	RefreshDeliveryOrderList();
}
