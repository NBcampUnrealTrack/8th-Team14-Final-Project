#include "UI/Ordering/CSWorldOrderWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Data/CSFacilityTypes.h"
#include "Features/Tools/CSUsableToolActor.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Systems/Inventory/CSFacilityInventorySubsystem.h"
#include "Systems/Inventory/CSStoreStockSubsystem.h"
#include "Systems/Ordering/CSOrderSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
// The existing row assets remain Blueprint-only. Keep their compatibility contract
// in these helpers rather than exposing Blueprint cart state to the native screen.
bool ConfigureRow(UUserWidget* Widget, FName PropertyName, const FDataTableRowHandle& Row)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Widget->GetClass(), PropertyName);
	if (!Property || Property->Struct != FDataTableRowHandle::StaticStruct()) return false;
	Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Widget), &Row);
	return true;
}

void SetRowObject(UUserWidget* Widget, FName Name, UObject* Value)
{
	if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), Name))
	{
		if (!Value || Value->IsA(Property->PropertyClass)) Property->SetObjectPropertyValue_InContainer(Widget, Value);
	}
}

bool BindRowSelection(UUserWidget* Row, FName Name, UObject* Target, FName Function)
{
	FMulticastInlineDelegateProperty* Property = FindFProperty<FMulticastInlineDelegateProperty>(Row->GetClass(), Name);
	UFunction* Handler = Target->FindFunction(Function);
	if (!Property || !Handler || !Property->SignatureFunction->IsSignatureCompatibleWith(Handler)) return false;
	FScriptDelegate Delegate;
	Delegate.BindUFunction(Target, Function);
	Property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Row)->AddUnique(Delegate);
	return true;
}

void SetRowVisualState(UUserWidget* Row, FName Name, bool Value)
{
	UFunction* Function = Row->FindFunction(Name);
	if (!Function || Function->NumParms != 1) return;
	FStructOnScope Parameters(Function);
	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_Parm))
		{
			FBoolProperty* Bool = CastField<FBoolProperty>(*It);
			if (!Bool || Bool->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)) return;
			Bool->SetPropertyValue_InContainer(Parameters.GetStructMemory(), Value);
			Row->ProcessEvent(Function, Parameters.GetStructMemory());
			return;
		}
	}
}
}

UCSWorldOrderWidget::UCSWorldOrderWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> Products(TEXT("/Game/ConvenienceStore/Data/DT_ProductMaster"));
	static ConstructorHelpers::FObjectFinder<UDataTable> Facilities(TEXT("/Game/ConvenienceStore/Data/DT_FantasyFacilityMaster"));
	static ConstructorHelpers::FObjectFinder<UCSProductVisualRegistry> Visuals(TEXT("/Game/ConvenienceStore/Data/DataAsset/DA_ProductVisualRegistry"));
	static ConstructorHelpers::FClassFinder<UUserWidget> ProductRow(TEXT("/Game/ConvenienceStore/UI/Order/WBP_OrderProductRowv2"));
	static ConstructorHelpers::FClassFinder<UUserWidget> FacilityRow(TEXT("/Game/ConvenienceStore/UI/Order/WBP_OrderFacilityRowv2"));
	NativeProductTable = Products.Object;
	NativeFacilityTable = Facilities.Object;
	NativeVisualRegistry = Visuals.Object;
	NativeProductRowClass = ProductRow.Class;
	NativeFacilityRowClass = FacilityRow.Class;
	// Existing WBP defaults, now editable on the native parent's Class Defaults.
	const TCHAR* FacilityNames[] = { TEXT("FAC_ChilledShelf"), TEXT("FAC_FreezerShelf"), TEXT("FAC_PotionShelf"),
		TEXT("FAC_ScrollRack"), TEXT("FAC_ToolRack"), TEXT("FAC_TrashCan") };
	for (const TCHAR* Name : FacilityNames)
	{
		const FString Path = FString::Printf(TEXT("/Game/Demo/KJW/Icon/Shelf_Icon/%s_Icon"), Name);
		ConstructorHelpers::FObjectFinder<UTexture2D> Icon(*Path);
		NativeFacilityIcons.Add(FName(Name), Icon.Object);
	}
}

void UCSWorldOrderWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// Screen owns these controls. Also discard obsolete serialized BP button bindings
	// during migration, so old and new checkout paths can never run together.
	auto BindButton = [this](FName Name, FName Handler)
	{
		if (UButton* Button = Cast<UButton>(GetWidgetFromName(Name)))
		{
			Button->OnClicked.Clear();
			FScriptDelegate Delegate;
			Delegate.BindUFunction(this, Handler);
			Button->OnClicked.AddUnique(Delegate);
		}
	};
	BindButton(TEXT("Btn_ProductTab"), GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, NativeProductTabClicked));
	BindButton(TEXT("Btn_FacilityTab"), GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, NativeFacilityTabClicked));
	BindButton(TEXT("Btn_Plus"), GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, NativePlusClicked));
	BindButton(TEXT("Btn_Minus"), GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, NativeMinusClicked));
	BindButton(TEXT("Btn_Order"), GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, NativeOrderClicked));
	BindButton(TEXT("Btn_Close"), GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, RequestNativeOrderClose));
	if (UCheckBox* Check = Cast<UCheckBox>(GetWidgetFromName(TEXT("CB_NextDayDelivery"))))
	{
		Check->OnCheckStateChanged.Clear();
		Check->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::NativeNextDayChanged);
	}
	if (UCheckBox* Check = Cast<UCheckBox>(GetWidgetFromName(TEXT("CB_ImmediateDelivery"))))
	{
		Check->OnCheckStateChanged.Clear();
		Check->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::NativeImmediateChanged);
	}
	if (!bNativeDefaultStyleCaptured)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_OrderButtonLabel"))))
		{
			NativeDefaultOrderLabel = Label->GetText();
			NativeDefaultOrderColor = Label->GetColorAndOpacity();
			bNativeDefaultStyleCaptured = true;
		}
	}
	if (NativeObservedState) NativeObservedState->OnFundsChanged.RemoveDynamic(this, &ThisClass::NativeFundsChanged);
	NativeObservedState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (NativeObservedState) NativeObservedState->OnFundsChanged.AddUniqueDynamic(this, &ThisClass::NativeFundsChanged);
	HideNativeNotice();
	SetNativeDelivery(ECSOrderDeliveryType::NextDay);
	RefreshNativeOrderScreen();
}

void UCSWorldOrderWidget::SetNativeText(FName Name, const FText& Text)
{
	if (UTextBlock* Block = Cast<UTextBlock>(GetWidgetFromName(Name))) Block->SetText(Text);
}

UTexture2D* UCSWorldOrderWidget::GetNativeIcon(const FDataTableRowHandle& Row, bool bFacility) const
{
	if (bFacility)
	{
		const auto* Data = Row.GetRow<FCSFacilityDataRow>(TEXT("Order icon"));
		if (Data && Data->DeliveredToolClass)
			return Data->DeliveredToolClass.GetDefaultObject()->GetToolIcon();
		const TObjectPtr<UTexture2D>* Icon = NativeFacilityIcons.Find(Row.RowName);
		return Icon ? Icon->Get() : nullptr;
	}
	const FCSProductVisualData* Visual = NativeVisualRegistry ? NativeVisualRegistry->FindVisualData(Row.RowName) : nullptr;
	return Visual ? Visual->ProductIcon.LoadSynchronous() : nullptr;
}

void UCSWorldOrderWidget::RefreshNativeOrderScreen()
{
	if (bRefreshingNativeView || !GetWorld()) return;
	TGuardValue<bool> Guard(bRefreshingNativeView, true);
	UUniformGridPanel* Grid = Cast<UUniformGridPanel>(GetWidgetFromName(TEXT("ScrollBox_ProductList")));
	UCSOrderSubsystem* Orders = GetWorld()->GetSubsystem<UCSOrderSubsystem>();
	if (!Grid || !Orders) return;
	Grid->ClearChildren();
	NativeVisibleRows.Reset();
	NativeVisibleHandles.Reset();
	UDataTable* Table = bNativeFacilityTab ? NativeFacilityTable.Get() : NativeProductTable.Get();
	UClass* RowClass = bNativeFacilityTab ? NativeFacilityRowClass.Get() : NativeProductRowClass.Get();
	const bool ValidTable = Table && Table->GetRowStruct() ==
		(bNativeFacilityTab ? FCSFacilityDataRow::StaticStruct() : FCSProductDataRow::StaticStruct());
	if (ValidTable && RowClass)
	{
		for (FName Name : Table->GetRowNames())
		{
			FDataTableRowHandle Handle;
			Handle.DataTable = Table;
			Handle.RowName = Name;
			// 시설은 모두 표시하고, 상품은 가챠 전용 등 발주 불가 항목만 제외합니다.
			if (!bNativeFacilityTab && !Orders->IsProductUnlocked(Handle)) continue;
			UUserWidget* Row = CreateWidget<UUserWidget>(GetOwningPlayer(), RowClass);
			if (!Row) continue;
			if (!ConfigureRow(Row, bNativeFacilityTab ? TEXT("FacilityRow") : TEXT("ProductRow"), Handle) ||
				!BindRowSelection(Row, bNativeFacilityTab ? TEXT("OnFacilitySelected") : TEXT("OnProductSelected"), this,
				bNativeFacilityTab ? GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, NativeFacilitySelected)
				: GET_FUNCTION_NAME_CHECKED(UCSWorldOrderWidget, NativeProductSelected)))
			{
				UE_LOG(LogTemp, Error, TEXT("Order row %s no longer matches its row/selection contract."), *GetNameSafe(RowClass));
				continue;
			}
			SetRowObject(Row, TEXT("ProductVisualRegistry"), NativeVisualRegistry);
			SetRowObject(Row, TEXT("RowIcon"), GetNativeIcon(Handle, bNativeFacilityTab));
			const int32 Index = NativeVisibleRows.Num();
			NativeVisibleRows.Add(Row);
			NativeVisibleHandles.Add(Handle);
			// Data is assigned before AddChild triggers the row's Construct event.
			Grid->AddChildToUniformGrid(Row, Index / 2, Index % 2);
		}
	}
	FDataTableRowHandle& Selected = bNativeFacilityTab ? NativeSelectedFacility : NativeSelectedProduct;
	if (!NativeVisibleHandles.ContainsByPredicate([&](const FDataTableRowHandle& Row)
		{ return Row.DataTable == Selected.DataTable && Row.RowName == Selected.RowName; }))
		Selected = NativeVisibleHandles.IsEmpty() ? FDataTableRowHandle() : NativeVisibleHandles[0];
	SetNativeText(TEXT("Text_ListTitle"), bNativeFacilityTab ? NSLOCTEXT("Order", "FacilityList", "시설 목록") : NSLOCTEXT("Order", "ProductList", "상품 목록"));
	SetNativeText(TEXT("Text_DetailTitle"), bNativeFacilityTab ? NSLOCTEXT("Order", "FacilityDetail", "선택한 시설") : NSLOCTEXT("Order", "ProductDetail", "선택한 상품"));
	auto Show = [this](FName Name, bool Visible)
	{
		if (UWidget* Widget = GetWidgetFromName(Name)) Widget->SetVisibility(Visible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	};
	Show(TEXT("Img_ProductTab"), bNativeFacilityTab);
	Show(TEXT("ProductTabSelected"), !bNativeFacilityTab);
	Show(TEXT("Img_FacilityTab"), !bNativeFacilityTab);
	Show(TEXT("FacilityTabSelected"), bNativeFacilityTab);
	if (UScrollBox* Scroll = Cast<UScrollBox>(GetWidgetFromName(TEXT("Scroll_ListViewport")))) Scroll->SetAlwaysShowScrollbar(true);
	OnOrderTabVisualsChanged(bNativeFacilityTab);
	RefreshNativeDetails();
	RefreshNativeRowStates();
	RefreshNativeTotals();
}

void UCSWorldOrderWidget::RefreshNativeRowStates()
{
	const FDataTableRowHandle& Selected = bNativeFacilityTab ? NativeSelectedFacility : NativeSelectedProduct;
	for (int32 Index = 0; Index < NativeVisibleRows.Num(); ++Index)
	{
		const auto& Row = NativeVisibleHandles[Index];
		SetRowVisualState(NativeVisibleRows[Index], TEXT("SetOrderSelected"), GetNativeOrderQuantity(Row, bNativeFacilityTab) > 0);
		SetRowVisualState(NativeVisibleRows[Index], TEXT("SetDetailFocused"), Row.RowName == Selected.RowName && Row.DataTable == Selected.DataTable);
	}
}

void UCSWorldOrderWidget::RefreshNativeDetails()
{
	const auto& Selected = bNativeFacilityTab ? NativeSelectedFacility : NativeSelectedProduct;
	const bool HasSelection = !Selected.IsNull();
	if (UWidget* Detail = GetWidgetFromName(TEXT("VB_SelectedProductDetail")))
		Detail->SetVisibility(HasSelection ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!HasSelection) return;
	if (bNativeFacilityTab)
	{
		const auto* Data = Selected.GetRow<FCSFacilityDataRow>(TEXT("Order detail"));
		if (!Data) return;
		SetNativeText(TEXT("Text_SelectedProductName"), Data->FacilityName);
		SetNativeText(TEXT("Text_SelectedBoxPrice"), FText::AsNumber(Data->PurchasePriceGold));
		const auto* Inventory = GetWorld()->GetSubsystem<UCSFacilityInventorySubsystem>();
		SetNativeText(TEXT("Text_SelectedProductMeta"), Data->DeliveredToolClass
			? Data->Description
			: FText::Format(NSLOCTEXT("Order", "Owned", "현재 보유 {0}개"), FText::AsNumber(Inventory ? Inventory->GetOwnedFacilityCount(Selected.RowName) : 0)));
	}
	else
	{
		const auto* Data = Selected.GetRow<FCSProductDataRow>(TEXT("Order detail"));
		if (!Data) return;
		SetNativeText(TEXT("Text_SelectedProductName"), Data->DisplayName);
		SetNativeText(TEXT("Text_SelectedBoxPrice"), FText::AsNumber(int64(Data->PurchasePrice) * Data->OrderPackQuantity));
		const auto* Stock = GetWorld()->GetSubsystem<UCSStoreStockSubsystem>();
		SetNativeText(TEXT("Text_SelectedProductMeta"), FText::Format(NSLOCTEXT("Order", "StockPack", "현재 보유 {0}개 / 1박스 {1}개"),
			FText::AsNumber(Stock ? Stock->GetProductStock(Selected.RowName).TotalQuantity : 0), FText::AsNumber(Data->OrderPackQuantity)));
	}
	if (UImage* Image = Cast<UImage>(GetWidgetFromName(TEXT("Img_SelectedProduct"))))
	{
		UTexture2D* Icon = GetNativeIcon(Selected, bNativeFacilityTab);
		Image->SetBrushFromTexture(Icon);
		Image->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	const int32 Quantity = GetNativeOrderQuantity(Selected, bNativeFacilityTab);
	SetNativeText(TEXT("Text_SelectedCrateCount"), FText::AsNumber(Quantity));
	if (UWidget* Plus = GetWidgetFromName(TEXT("Btn_Plus"))) Plus->SetIsEnabled(Quantity < NativeMaxQuantity);
	if (UWidget* Minus = GetWidgetFromName(TEXT("Btn_Minus"))) Minus->SetIsEnabled(Quantity > 0);
}

void UCSWorldOrderWidget::RefreshNativeTotals()
{
	const auto Quote = EvaluateNativeOrderCart(NativeDelivery);
	SetNativeText(TEXT("Text_TotalOrderCost"), FText::AsNumber(Quote.TotalCost));
	SetNativeText(TEXT("Text_CurrentFunds"), FText::AsNumber(Quote.CurrentFunds));
	SetNativeText(TEXT("Text_SummaryCurrentFunds"), FText::AsNumber(Quote.CurrentFunds));
	SetNativeText(TEXT("Text_RemainingFunds"), FText::AsNumber(Quote.RemainingFunds));
	// Invalid quotes remain clickable so submission can explain why the order failed.
	if (UWidget* Button = GetWidgetFromName(TEXT("Btn_Order"))) Button->SetIsEnabled(!bSubmittingNativeOrder);
	OnOrderAffordabilityChanged(Quote.bSuccess);
}

void UCSWorldOrderWidget::NativeProductTabClicked() { bNativeFacilityTab = false; RefreshNativeOrderScreen(); }
void UCSWorldOrderWidget::NativeFacilityTabClicked() { bNativeFacilityTab = true; RefreshNativeOrderScreen(); }
void UCSWorldOrderWidget::NativeProductSelected(FDataTableRowHandle Row)
{
	if (bNativeFacilityTab || !NativeVisibleHandles.ContainsByPredicate([&](const auto& Entry) { return Entry.DataTable == Row.DataTable && Entry.RowName == Row.RowName; })) return;
	NativeSelectedProduct = Row; RefreshNativeDetails(); RefreshNativeRowStates();
}
void UCSWorldOrderWidget::NativeFacilitySelected(FDataTableRowHandle Row)
{
	if (!bNativeFacilityTab || !NativeVisibleHandles.ContainsByPredicate([&](const auto& Entry) { return Entry.DataTable == Row.DataTable && Entry.RowName == Row.RowName; })) return;
	NativeSelectedFacility = Row; RefreshNativeDetails(); RefreshNativeRowStates();
}
void UCSWorldOrderWidget::ChangeNativeSelectedQuantity(int32 Delta)
{
	const auto& Row = bNativeFacilityTab ? NativeSelectedFacility : NativeSelectedProduct;
	if (Row.IsNull() || bSubmittingNativeOrder || bOpening || bClosing) return;
	SetNativeOrderQuantity(Row, bNativeFacilityTab, int32(FMath::Clamp<int64>(int64(GetNativeOrderQuantity(Row, bNativeFacilityTab)) + Delta, 0, FMath::Max(1, NativeMaxQuantity))));
	RefreshNativeDetails(); RefreshNativeRowStates(); RefreshNativeTotals();
}
void UCSWorldOrderWidget::NativePlusClicked() { ChangeNativeSelectedQuantity(1); }
void UCSWorldOrderWidget::NativeMinusClicked() { ChangeNativeSelectedQuantity(-1); }
void UCSWorldOrderWidget::SetNativeDelivery(ECSOrderDeliveryType Delivery)
{
	TGuardValue<bool> Guard(bUpdatingDelivery, true);
	NativeDelivery = Delivery;
	if (auto* Check = Cast<UCheckBox>(GetWidgetFromName(TEXT("CB_NextDayDelivery")))) Check->SetIsChecked(Delivery == ECSOrderDeliveryType::NextDay);
	if (auto* Check = Cast<UCheckBox>(GetWidgetFromName(TEXT("CB_ImmediateDelivery")))) Check->SetIsChecked(Delivery == ECSOrderDeliveryType::Immediate);
	RefreshNativeTotals();
}
void UCSWorldOrderWidget::NativeNextDayChanged(bool bChecked) { if (!bUpdatingDelivery) SetNativeDelivery(bChecked ? ECSOrderDeliveryType::NextDay : NativeDelivery); }
void UCSWorldOrderWidget::NativeImmediateChanged(bool bChecked) { if (!bUpdatingDelivery) SetNativeDelivery(bChecked ? ECSOrderDeliveryType::Immediate : NativeDelivery); }
void UCSWorldOrderWidget::NativeFundsChanged(int32 NewFunds) { if (!bSubmittingNativeOrder) RefreshNativeTotals(); }
void UCSWorldOrderWidget::NativeOrderClicked()
{
	const auto Result = SubmitNativeOrderCart(NativeDelivery);
	ShowNativeNotice(Result);
}
void UCSWorldOrderWidget::ShowNativeNotice(const FCSCombinedOrderResult& Result)
{
	GetWorld()->GetTimerManager().ClearTimer(NativeNoticeTimer);
	if (UWidget* Panel = GetWidgetFromName(TEXT("OrderNoticePanel"))) Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (GetWidgetFromName(TEXT("OrderNoticeText"))) SetNativeText(TEXT("OrderNoticeText"), Result.Message);
	else SetNativeText(TEXT("Text_OrderButtonLabel"), Result.Message); // Usable before the optional design is added.
	OnOrderNoticeChanged(Result.Message, Result.bSuccess, true);
	GetWorld()->GetTimerManager().SetTimer(NativeNoticeTimer, this, &ThisClass::HideNativeNotice, FMath::Max(0.1f, NativeNoticeDuration), false);
}
void UCSWorldOrderWidget::HideNativeNotice()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(NativeNoticeTimer);
	if (UWidget* Panel = GetWidgetFromName(TEXT("OrderNoticePanel"))) Panel->SetVisibility(ESlateVisibility::Collapsed);
	if (bNativeDefaultStyleCaptured)
	{
		SetNativeText(TEXT("Text_OrderButtonLabel"), NativeDefaultOrderLabel);
		if (auto* Label = Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_OrderButtonLabel")))) Label->SetColorAndOpacity(NativeDefaultOrderColor);
	}
	OnOrderNoticeChanged(FText::GetEmpty(), false, false);
}
