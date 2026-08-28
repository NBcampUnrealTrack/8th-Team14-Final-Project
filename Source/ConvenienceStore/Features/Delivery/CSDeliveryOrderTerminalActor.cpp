#include "Features/Delivery/CSDeliveryOrderTerminalActor.h"

#include "Blueprint/UserWidget.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Features/Delivery/CSDeliveryOrderIndicatorActor.h"
#include "Features/UI/CSDeliveryOrderWidget.h"
#include "GameFramework/Pawn.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

ACSDeliveryOrderTerminalActor::ACSDeliveryOrderTerminalActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ACSDeliveryOrderTerminalActor::BeginPlay()
{
	Super::BeginPlay();
	BoundDeliveryOrderSubsystem = GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	if (IsValid(BoundDeliveryOrderSubsystem))
	{
		BoundDeliveryOrderSubsystem->OnDeliveryOrdersChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleDeliveryOrdersChanged);
	}
	RefreshDeliveryOrderIndicator();
}

void ACSDeliveryOrderTerminalActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(BoundDeliveryOrderSubsystem))
	{
		BoundDeliveryOrderSubsystem->OnDeliveryOrdersChanged.RemoveDynamic(
			this,
			&ThisClass::HandleDeliveryOrdersChanged);
	}
	BoundDeliveryOrderSubsystem = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ACSDeliveryOrderTerminalActor::Interact_Implementation(AActor* Interactor)
{
	UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	// 주문이 없거나 이미 수락한 상태여도 단말기는 항상 열 수 있어야 합니다.
	// 위젯에서 현재 주문 상태를 읽어 "주문 없음" 또는 진행 중 주문을 표시합니다.
	if (!IsValid(DeliveryOrderSubsystem) || !IsValid(DeliveryOrderWidgetClass))
	{
		return;
	}

	if (IsValid(ActiveDeliveryOrderWidget))
	{
		CloseDeliveryOrderUI(ActiveDeliveryOrderWidget);
	}

	APlayerController* PlayerController = nullptr;
	if (const APawn* InteractingPawn = Cast<APawn>(Interactor))
	{
		PlayerController = Cast<APlayerController>(InteractingPawn->GetController());
	}
	if (!IsValid(PlayerController))
	{
		return;
	}

	ActiveDeliveryOrderWidget = CreateWidget<UCSDeliveryOrderWidget>(
		PlayerController,
		DeliveryOrderWidgetClass);
	if (!IsValid(ActiveDeliveryOrderWidget))
	{
		return;
	}

	ActiveDeliveryOrderWidget->InitializeDeliveryOrderTerminal(this);
	ActiveDeliveryOrderWidget->AddToViewport();
	if (ACSStorePlayerController* StorePlayerController = Cast<ACSStorePlayerController>(PlayerController))
	{
		StorePlayerController->SetGameplayUIInputLock(ActiveDeliveryOrderWidget, true);
	}
}

FText ACSDeliveryOrderTerminalActor::GetInteractionText_Implementation() const
{
	const UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	if (!IsValid(DeliveryOrderSubsystem) || !DeliveryOrderSubsystem->HasActiveOrder())
	{
		// 플레이어 HUD가 앞에 [E]를 자동으로 붙이므로, 여기서는 행동 이름만 반환합니다.
		return NSLOCTEXT("Delivery", "ReviewNoOrder", "배달 주문 확인");
	}
	const FCSDeliveryOrderRuntime Order = DeliveryOrderSubsystem->GetCurrentOrder();
	if (Order.Status == ECSDeliveryOrderStatus::Accepted)
	{
		return NSLOCTEXT("Delivery", "OrderAccepted", "배달 주문을 진행 중입니다.");
	}
	// 플레이어 HUD가 앞에 [E]를 자동으로 붙이므로, 여기서는 행동 이름만 반환합니다.
	return NSLOCTEXT("Delivery", "ReviewOrder", "배달 주문 확인");
}

FCSDeliveryOrderRuntime ACSDeliveryOrderTerminalActor::GetActiveDeliveryOrder() const
{
	const UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetCurrentOrder()
		: FCSDeliveryOrderRuntime();
}

TArray<FCSDeliveryOrderRuntime> ACSDeliveryOrderTerminalActor::GetDeliveryOrders() const
{
	const UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetDeliveryOrders()
		: TArray<FCSDeliveryOrderRuntime>();
}

int32 ACSDeliveryOrderTerminalActor::GetTodayGeneratedOrderCount() const
{
	const UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetTodayGeneratedOrderCount()
		: 0;
}

int32 ACSDeliveryOrderTerminalActor::GetAcceptedOrderCount() const
{
	const UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem)
		? DeliveryOrderSubsystem->GetAcceptedOrderCount()
		: 0;
}

bool ACSDeliveryOrderTerminalActor::CanAcceptActiveDeliveryOrder(FText& OutReason) const
{
	const UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	if (!IsValid(DeliveryOrderSubsystem))
	{
		OutReason = NSLOCTEXT("Delivery", "NoDeliverySubsystem", "배달 주문 시스템을 찾을 수 없습니다.");
		return false;
	}
	return DeliveryOrderSubsystem->CanAcceptCurrentOrder(OutReason);
}

bool ACSDeliveryOrderTerminalActor::AcceptActiveDeliveryOrder(FText& OutMessage)
{
	UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->AcceptCurrentOrder(OutMessage);
}

bool ACSDeliveryOrderTerminalActor::DeclineActiveDeliveryOrder(FText& OutMessage)
{
	UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->DeclineCurrentOrder(OutMessage);
}

bool ACSDeliveryOrderTerminalActor::CancelActiveDeliveryOrder(FText& OutMessage)
{
	UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->CancelCurrentOrder(OutMessage);
}

bool ACSDeliveryOrderTerminalActor::CanAcceptDeliveryOrder(const int32 OrderId, FText& OutReason) const
{
	const UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->CanAcceptOrder(OrderId, OutReason);
}

bool ACSDeliveryOrderTerminalActor::AcceptDeliveryOrder(const int32 OrderId, FText& OutMessage)
{
	UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->AcceptOrder(OrderId, OutMessage);
}

bool ACSDeliveryOrderTerminalActor::DeclineDeliveryOrder(const int32 OrderId, FText& OutMessage)
{
	UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->DeclineOrder(OrderId, OutMessage);
}

bool ACSDeliveryOrderTerminalActor::CancelDeliveryOrder(const int32 OrderId, FText& OutMessage)
{
	UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliveryOrderSubsystem) && DeliveryOrderSubsystem->CancelOrder(OrderId, OutMessage);
}

void ACSDeliveryOrderTerminalActor::CloseDeliveryOrderUI(UCSDeliveryOrderWidget* RequestingWidget)
{
	if (!IsValid(RequestingWidget) || RequestingWidget != ActiveDeliveryOrderWidget)
	{
		return;
	}

	if (ACSStorePlayerController* StorePlayerController =
		Cast<ACSStorePlayerController>(RequestingWidget->GetOwningPlayer()))
	{
		StorePlayerController->SetGameplayUIInputLock(RequestingWidget, false);
	}
	RequestingWidget->RemoveFromParent();
	ActiveDeliveryOrderWidget = nullptr;
}

void ACSDeliveryOrderTerminalActor::HandleDeliveryOrdersChanged()
{
	RefreshDeliveryOrderIndicator();
}

void ACSDeliveryOrderTerminalActor::RefreshDeliveryOrderIndicator()
{
	if (!IsValid(BoundDeliveryOrderSubsystem))
	{
		return;
	}

	const bool bHasPendingOrder = BoundDeliveryOrderSubsystem->GetDeliveryOrders().ContainsByPredicate(
		[](const FCSDeliveryOrderRuntime& Order)
		{
			return Order.Status == ECSDeliveryOrderStatus::PendingAcceptance;
		});

	if (!IsValid(DeliveryOrderIndicator) && bHasPendingOrder && IsValid(DeliveryOrderIndicatorClass))
	{
		DeliveryOrderIndicator = GetWorld()->SpawnActor<ACSDeliveryOrderIndicatorActor>(
			DeliveryOrderIndicatorClass,
			GetActorLocation(),
			GetActorRotation());
		if (IsValid(DeliveryOrderIndicator))
		{
			DeliveryOrderIndicator->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			DeliveryOrderIndicator->SetActorRelativeLocation(DeliveryOrderIndicatorOffset);
		}
	}

	if (IsValid(DeliveryOrderIndicator))
	{
		DeliveryOrderIndicator->SetIndicatorVisible(bHasPendingOrder);
	}
}
