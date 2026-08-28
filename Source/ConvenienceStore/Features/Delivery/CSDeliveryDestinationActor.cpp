#include "Features/Delivery/CSDeliveryDestinationActor.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Features/Delivery/CSDeliveryCartActor.h"
#include "Features/Delivery/CSDeliveryOrderIndicatorActor.h"
#include "Features/UI/CSDeliverySettlementWidget.h"
#include "GameFramework/Pawn.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

ACSDeliveryDestinationActor::ACSDeliveryDestinationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	// 새 배달 지점은 별도 설정 전에도 UI에서 알아볼 수 있는 기본 이름을 사용합니다.
	DestinationDisplayName = NSLOCTEXT("Delivery", "DefaultDestinationDisplayName", "배달 지점");

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DeliveryZone = CreateDefaultSubobject<UBoxComponent>(TEXT("DeliveryZone"));
	DeliveryZone->SetupAttachment(SceneRoot);
	DeliveryZone->SetBoxExtent(FVector(250.0f, 250.0f, 150.0f));
	DeliveryZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DeliveryZone->SetCollisionResponseToAllChannels(ECR_Overlap);
	DeliveryZone->SetGenerateOverlapEvents(true);

	DeliveryZoneGuide = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeliveryZoneGuide"));
	DeliveryZoneGuide->SetupAttachment(SceneRoot);
	DeliveryZoneGuide->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeliveryZoneGuide->SetCastShadow(false);
	DeliveryZoneGuide->SetHiddenInGame(true);
}

void ACSDeliveryDestinationActor::BeginPlay()
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

void ACSDeliveryDestinationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

void ACSDeliveryDestinationActor::Interact_Implementation(AActor* Interactor)
{
	FText CompletionMessage;
	bool bCompleted = false;
	int32 RewardGold = 0;
	int32 ReputationReward = 0;
	if (UCSDeliveryOrderSubsystem* DeliveryOrderSubsystem = GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
		IsValid(DeliveryOrderSubsystem))
	{
		FCSDeliveryOrderRuntime AssignedOrder;
		if (DeliveryOrderSubsystem->GetDeliveryOrderById(AssignedOrderId, AssignedOrder))
		{
			// 완료 처리 시 주문이 제거되므로, 정산 UI에 표시할 보상은 미리 보관합니다.
			RewardGold = AssignedOrder.RewardGold;
			ReputationReward = AssignedOrder.ReputationReward;
		}

		TArray<AActor*> OverlappingCarts;
		DeliveryZone->GetOverlappingActors(OverlappingCarts, ACSDeliveryCartActor::StaticClass());
		ACSDeliveryCartActor* DeliveryCart = OverlappingCarts.IsEmpty()
			? nullptr
			: Cast<ACSDeliveryCartActor>(OverlappingCarts[0]);
		bCompleted = DeliveryOrderSubsystem->TryCompleteCurrentOrder(this, DeliveryCart, CompletionMessage);
		if (bCompleted)
		{
			OnDeliveryCompleted();
		}
	}
	else
	{
		CompletionMessage = NSLOCTEXT("Delivery", "MissingDeliveryOrderSubsystem", "배달 주문 시스템을 찾을 수 없습니다.");
	}

	ShowDeliverySettlement(
		Interactor,
		CompletionMessage,
		bCompleted ? ECSDeliverySettlementResult::Completed : ECSDeliverySettlementResult::Failed,
		bCompleted ? RewardGold : 0,
		bCompleted ? ReputationReward : 0);
}

void ACSDeliveryDestinationActor::ShowDeliverySettlement(
	AActor* Interactor,
	const FText& Message,
	const ECSDeliverySettlementResult Result,
	const int32 RewardGold,
	const int32 ReputationReward)
{
	const APawn* InteractingPawn = Cast<APawn>(Interactor);
	ACSStorePlayerController* StorePlayerController = InteractingPawn == nullptr
		? nullptr
		: Cast<ACSStorePlayerController>(InteractingPawn->GetController());
	ShowDeliverySettlementForPlayer(
		StorePlayerController,
		Message,
		Result,
		RewardGold,
		ReputationReward);
}

void ACSDeliveryDestinationActor::ShowDeliverySettlementForPlayer(
	ACSStorePlayerController* StorePlayerController,
	const FText& Message,
	const ECSDeliverySettlementResult Result,
	const int32 RewardGold,
	const int32 ReputationReward)
{
	if (!IsValid(StorePlayerController) || !IsValid(DeliverySettlementWidgetClass))
	{
		return;
	}

	if (IsValid(ActiveDeliverySettlementWidget))
	{
		ActiveDeliverySettlementWidget->CloseSettlementWindow();
	}

	ActiveDeliverySettlementWidget = CreateWidget<UCSDeliverySettlementWidget>(
		StorePlayerController,
		DeliverySettlementWidgetClass);
	if (!IsValid(ActiveDeliverySettlementWidget))
	{
		return;
	}

	ActiveDeliverySettlementWidget->InitializeDeliverySettlement(
		StorePlayerController,
		Message,
		Result == ECSDeliverySettlementResult::Completed,
		Result,
		RewardGold,
		ReputationReward);
	ActiveDeliverySettlementWidget->AddToViewport();
	// 기한 초과 결과도 확인 버튼을 누를 때까지 유지하므로 게임 입력을 함께 잠급니다.
	StorePlayerController->SetGameplayUIInputLock(ActiveDeliverySettlementWidget, true);
}

FText ACSDeliveryDestinationActor::GetInteractionText_Implementation() const
{
	if (AssignedOrderId == INDEX_NONE)
	{
		return NSLOCTEXT("Delivery", "NoAssignedOrder", "진행 중인 배달 주문이 없습니다.");
	}
	// 플레이어 HUD가 앞에 [E]를 자동으로 붙이므로, 여기서는 행동 이름만 반환합니다.
	return NSLOCTEXT("Delivery", "CompleteDelivery", "배달 완료");
}

void ACSDeliveryDestinationActor::SetDeliveryOrderHighlight(
	const bool bShouldHighlight,
	const int32 StencilValue)
{
	bIsHighlightedForDelivery = bShouldHighlight;
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent) || PrimitiveComponent == DeliveryZone)
		{
			continue;
		}
		PrimitiveComponent->SetRenderCustomDepth(bShouldHighlight);
		if (bShouldHighlight)
		{
			PrimitiveComponent->SetCustomDepthStencilValue(StencilValue);
		}
	}
	SetDeliveryZoneGuideVisible(bShouldHighlight);
}

void ACSDeliveryDestinationActor::AssignDeliveryOrder(const int32 OrderId)
{
	AssignedOrderId = OrderId;
}

void ACSDeliveryDestinationActor::ClearDeliveryOrder(const int32 OrderId)
{
	if (AssignedOrderId == OrderId)
	{
		AssignedOrderId = INDEX_NONE;
	}
}

void ACSDeliveryDestinationActor::HandleDeliveryOrdersChanged()
{
	RefreshDeliveryOrderIndicator();
}

void ACSDeliveryDestinationActor::RefreshDeliveryOrderIndicator()
{
	if (!IsValid(BoundDeliveryOrderSubsystem))
	{
		return;
	}

	FCSDeliveryOrderRuntime AssignedOrder;
	const bool bShouldShowIndicator = AssignedOrderId != INDEX_NONE &&
		BoundDeliveryOrderSubsystem->GetDeliveryOrderById(AssignedOrderId, AssignedOrder) &&
		AssignedOrder.Status == ECSDeliveryOrderStatus::Accepted;

	if (!IsValid(DeliveryOrderIndicator) && bShouldShowIndicator && IsValid(DeliveryOrderIndicatorClass))
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
		DeliveryOrderIndicator->SetIndicatorVisible(bShouldShowIndicator);
	}
}

void ACSDeliveryDestinationActor::SetDeliveryZoneGuideVisible(const bool bShouldShow)
{
	if (IsValid(DeliveryZoneGuide))
	{
		DeliveryZoneGuide->SetHiddenInGame(!bShouldShow);
	}
}
