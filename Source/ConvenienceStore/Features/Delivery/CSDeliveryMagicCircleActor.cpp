#include "Features/Delivery/CSDeliveryMagicCircleActor.h"

#include "Blueprint/UserWidget.h"
#include "Components/BoxComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSDeliveryOrderSettingsDataAsset.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/UI/CSDeliverySettlementWidget.h"
#include "GameFramework/Pawn.h"
#include "Systems/Delivery/CSDeliveryOrderSubsystem.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"

ACSDeliveryMagicCircleActor::ACSDeliveryMagicCircleActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	LevelVFX.SetNum(5);
	MagicCircleVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("MagicCircleVFX"));
	MagicCircleVFX->SetupAttachment(SceneRoot);
	MagicCircleVFX->SetAutoActivate(false);

	MagicCircleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MagicCircleMesh"));
	MagicCircleMesh->SetupAttachment(SceneRoot);
	MagicCircleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DeliveryZone = CreateDefaultSubobject<UBoxComponent>(TEXT("DeliveryZone"));
	DeliveryZone->SetupAttachment(SceneRoot);
	DeliveryZone->SetBoxExtent(FVector(180.0f, 180.0f, 80.0f));
	DeliveryZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DeliveryZone->SetCollisionResponseToAllChannels(ECR_Overlap);
	// 플레이어 상호작용은 Visibility 라인 트레이스로 대상을 찾으므로, 감지 범위가 조준 대상도 겸합니다.
	// 상품 감지 영역은 조준선을 막지 않아, 위에 놓인 상품을 다시 집을 수 있게 합니다.
	DeliveryZone->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	DeliveryZone->SetGenerateOverlapEvents(true);

	InteractionCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(SceneRoot);
	// 바닥에 가까운 얇은 판만 상호작용 대상으로 둡니다. 상품은 이 판보다 먼저 조준됩니다.
	InteractionCollision->SetBoxExtent(FVector(180.0f, 180.0f, 5.0f));
	InteractionCollision->SetRelativeLocation(FVector(0.0f, 0.0f, 5.0f));
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);

	DeliveryDropPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("DeliveryDropPoint"));
	DeliveryDropPoint->SetupAttachment(SceneRoot);
	DeliveryDropPoint->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	DeliveryDropPoint->SetHiddenInGame(true);
}

void ACSDeliveryMagicCircleActor::BeginPlay()
{
	Super::BeginPlay();
	StoreState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (IsValid(StoreState))
	{
		StoreState->OnMagicCircleUpgraded.AddUniqueDynamic(this, &ThisClass::HandleMagicCircleUpgraded);
		UpdateLevelVFX(StoreState->GetMagicCircleLevel());
	}
	if (UCSDeliveryOrderSubsystem* DeliverySubsystem = GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>())
	{
		DeliverySubsystem->ConfigureOrderSettings(OrderSettings);
		DeliverySubsystem->OnDeliveryOrdersChanged.AddUniqueDynamic(this, &ThisClass::RefreshReadyState);
	}
	// 상품 이동·파괴도 감지하되 매 프레임 검사하지 않습니다.
	GetWorldTimerManager().SetTimer(ReadyStateTimer, this, &ThisClass::RefreshReadyState, 0.1f, true);
	RefreshReadyState();
}

void ACSDeliveryMagicCircleActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(StoreState)) StoreState->OnMagicCircleUpgraded.RemoveDynamic(this, &ThisClass::HandleMagicCircleUpgraded);
	GetWorldTimerManager().ClearTimer(ReadyStateTimer);
	if (auto* Orders = GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>())
	{
		Orders->OnDeliveryOrdersChanged.RemoveDynamic(this, &ThisClass::RefreshReadyState);
	}
	Super::EndPlay(EndPlayReason);
}

void ACSDeliveryMagicCircleActor::PlayOneShotVFX(UNiagaraSystem* Effect)
{
	if (IsValid(Effect)) UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Effect,
		MagicCircleVFX->GetComponentLocation(), MagicCircleVFX->GetComponentRotation(), MagicCircleVFX->GetComponentScale());
}

void ACSDeliveryMagicCircleActor::UpdateLevelVFX(int32 Level)
{
	MagicCircleVFX->DeactivateImmediate();
	const FCSMagicCircleLevelVFX* Effects = LevelVFX.IsValidIndex(Level - 1) ? &LevelVFX[Level - 1] : nullptr;
	MagicCircleVFX->SetAsset(Effects ? Effects->IdleEffect.Get() : nullptr);
	if (Effects && IsValid(Effects->IdleEffect)) MagicCircleVFX->Activate(true);
	OnMagicCircleVisualUpdated(Level);
	OnDeliveryReadyChanged(bProductsReady);
}

void ACSDeliveryMagicCircleActor::HandleMagicCircleUpgraded(int32 Level) { UpdateLevelVFX(Level); }

void ACSDeliveryMagicCircleActor::GetAvailableProducts(TArray<ACSProductPickupActor*>& OutProducts) const
{
	OutProducts.Reset();
	TArray<AActor*> OverlappingActors;
	DeliveryZone->GetOverlappingActors(OverlappingActors, ACSProductPickupActor::StaticClass());
	for (AActor* Actor : OverlappingActors)
	{
		if (auto* Product = Cast<ACSProductPickupActor>(Actor); IsValid(Product)) OutProducts.Add(Product);
	}
}

bool ACSDeliveryMagicCircleActor::HasRequiredProducts() const
{
	const auto* Orders = GetWorld() ? GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>() : nullptr;
	if (!Orders) return false;
	TArray<ACSProductPickupActor*> Products, MatchedProducts;
	GetAvailableProducts(Products);
	FText Message;
	return Orders->FindDispatchProducts(Products, MatchedProducts, Message);
}

void ACSDeliveryMagicCircleActor::RefreshReadyState()
{
	const bool bReady = HasRequiredProducts();
	if (bReady == bProductsReady) return;
	bProductsReady = bReady;
	if (bReady)
	{
		const int32 Index = IsValid(StoreState) ? StoreState->GetMagicCircleLevel() - 1 : 0;
		if (LevelVFX.IsValidIndex(Index)) PlayOneShotVFX(LevelVFX[Index].ReadyEffect);
	}
	OnDeliveryReadyChanged(bReady);
}

void ACSDeliveryMagicCircleActor::Interact_Implementation(AActor* Interactor)
{
	UCSDeliveryOrderSubsystem* DeliverySubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	// 수락한 주문의 상품이 모두 모였을 때만 발송합니다.
	// 그 전에는 HUD 안내만 보이고, E를 눌러도 실패 정산 UI가 열리지 않습니다.
	if (!IsValid(DeliverySubsystem) || !HasRequiredProducts())
	{
		return;
	}

	FText Message;
	int32 RewardGold = 0;
	int32 ReputationReward = 0;
	bool bSucceeded = false;
	if (IsValid(DeliverySubsystem))
	{
		TArray<ACSProductPickupActor*> Products;
		GetAvailableProducts(Products);
		bSucceeded = DeliverySubsystem->TryCompleteDispatchOrder(Products, Message, RewardGold, ReputationReward);
	}
	else
	{
		Message = NSLOCTEXT("Delivery", "MissingDeliverySubsystem", "배달 주문 시스템을 찾을 수 없습니다.");
	}

	const APawn* Pawn = Cast<APawn>(Interactor);
	if (bSucceeded)
	{
		if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::DeliveryDispatch);
		const int32 Index = IsValid(StoreState) ? StoreState->GetMagicCircleLevel() - 1 : 0;
		if (LevelVFX.IsValidIndex(Index)) PlayOneShotVFX(LevelVFX[Index].DispatchEffect);
		OnDeliveryDispatched();
	}
	ACSStorePlayerController* Controller = Pawn == nullptr ? nullptr : Cast<ACSStorePlayerController>(Pawn->GetController());
	if (IsValid(Controller) && IsValid(DeliverySettlementWidgetClass))
	{
		if (UCSDeliverySettlementWidget* Settlement = CreateWidget<UCSDeliverySettlementWidget>(Controller, DeliverySettlementWidgetClass))
		{
			Settlement->InitializeDeliverySettlement(Controller, Message, bSucceeded,
				bSucceeded ? ECSDeliverySettlementResult::Completed : ECSDeliverySettlementResult::Failed,
				bSucceeded ? RewardGold : 0, bSucceeded ? ReputationReward : 0);
			Settlement->AddToViewport();
			Controller->SetGameplayUIInputLock(Settlement, true);
		}
	}
}

FText ACSDeliveryMagicCircleActor::GetInteractionText_Implementation() const
{
	// 빈 문자열은 일반 상호작용(E) 프롬프트를 표시하지 않는다는 뜻입니다.
	return HasRequiredProducts()
		? NSLOCTEXT("Delivery", "DispatchThroughCircle", "상품 준비 완료 · 발송")
		: FText::GetEmpty();
}

FTransform ACSDeliveryMagicCircleActor::GetDeliveryPreviewTransform() const
{
	return IsValid(DeliveryDropPoint) ? DeliveryDropPoint->GetComponentTransform() : GetActorTransform();
}

FText ACSDeliveryMagicCircleActor::GetUnavailableInteractionText() const
{
	const UCSDeliveryOrderSubsystem* DeliverySubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	if (!IsValid(DeliverySubsystem) || !DeliverySubsystem->HasActiveOrder())
	{
		return NSLOCTEXT("Delivery", "NoDeliveryOrders", "배달 주문이 없습니다.");
	}

	return IsDispatchReady()
		? NSLOCTEXT("Delivery", "NeedDeliveryProducts", "주문 상품이 부족합니다")
		: NSLOCTEXT("Delivery", "AcceptDispatchOrder", "게시판에서 배달 주문을 수락하세요");
}

bool ACSDeliveryMagicCircleActor::IsDispatchReady() const
{
	const UCSDeliveryOrderSubsystem* DeliverySubsystem = GetWorld() == nullptr
		? nullptr
		: GetWorld()->GetSubsystem<UCSDeliveryOrderSubsystem>();
	return IsValid(DeliverySubsystem) && DeliverySubsystem->GetActiveDispatchOrderId() != INDEX_NONE;
}

bool ACSDeliveryMagicCircleActor::PlaceProduct(ACSProductPickupActor* Product)
{
	if (!IsDispatchReady() || !IsValid(Product)) return false;
	Product->PrepareForPickupPhysics();
	Product->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Product->SetActorLocation(GetDeliveryPreviewTransform().GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	Product->SetActorEnableCollision(true);
	Product->SetHeldByPlayerForFreshness(false);
	Product->EnableWorldDropPhysics();
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::ProductStock);
	return true;
}

bool ACSDeliveryMagicCircleActor::PlaceOneProductFromCrate(ACSDeliveryCrateActor* DeliveryCrate)
{
	if (!IsDispatchReady() || !IsValid(DeliveryCrate) || DeliveryCrate->GetRemainingQuantity() < 1) return false;
	ACSProductPickupActor* Product = DeliveryCrate->SpawnProductActor();
	if (!IsValid(Product)) return false;
	Product->PrepareForPickupPhysics();
	Product->SetActorLocation(GetDeliveryPreviewTransform().GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	Product->SetActorEnableCollision(true);
	Product->SetHeldByPlayerForFreshness(false);
	Product->EnableWorldDropPhysics();
	if (!DeliveryCrate->ConsumeOne())
	{
		Product->Destroy();
		return false;
	}
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::ProductStock);
	return true;
}
