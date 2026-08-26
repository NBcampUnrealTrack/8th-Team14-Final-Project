#include "Systems/StoreFlow/CSDailySettlementSubsystem.h"

#include "EngineUtils.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Trash/CSTrashCanActor.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/Ordering/CSOrderSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

void UCSDailySettlementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 플레이 중 새로 생성되는 쓰레기통도 정산 이벤트에 연결합니다.
	if (UWorld* World = GetWorld())
	{
		ActorSpawnedDelegateHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(
				this, &UCSDailySettlementSubsystem::HandleActorSpawned));
	}
}

void UCSDailySettlementSubsystem::Deinitialize()
{
	// 월드 종료 후 폐기 이벤트가 남지 않도록 모든 구독을 해제합니다.
	if (UWorld* World = GetWorld();
		World != nullptr && ActorSpawnedDelegateHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedDelegateHandle);
	}
	ActorSpawnedDelegateHandle.Reset();

	for (ACSTrashCanActor* TrashCan : BoundTrashCans)
	{
		if (IsValid(TrashCan))
		{
			TrashCan->OnItemDisposed.RemoveDynamic(
				this, &UCSDailySettlementSubsystem::HandleItemDisposed);
		}
	}
	BoundTrashCans.Reset();

	if (IsValid(OrderSubsystem))
	{
		OrderSubsystem->OnOrderPlaced.RemoveDynamic(
			this, &UCSDailySettlementSubsystem::HandleProductOrderPlaced);
		OrderSubsystem->OnFacilityOrderPlaced.RemoveDynamic(
			this, &UCSDailySettlementSubsystem::HandleFacilityOrderPlaced);
	}
	if (IsValid(CustomerRuntimeSubsystem))
	{
		CustomerRuntimeSubsystem->OnShopliftEscaped.RemoveDynamic(
			this, &UCSDailySettlementSubsystem::HandleShopliftEscaped);
	}
	OrderSubsystem = nullptr;
	CustomerRuntimeSubsystem = nullptr;

	Super::Deinitialize();
}

void UCSDailySettlementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 레벨에 미리 배치된 쓰레기통을 정산 이벤트에 연결합니다.
	for (TActorIterator<ACSTrashCanActor> It(&InWorld); It; ++It)
	{
		BindTrashCan(*It);
	}

	// 팀 시스템의 완료 이벤트만 구독해 발주와 절도 로직을 직접 수정하지 않습니다.
	OrderSubsystem = InWorld.GetSubsystem<UCSOrderSubsystem>();
	if (IsValid(OrderSubsystem))
	{
		OrderSubsystem->OnOrderPlaced.AddUniqueDynamic(
			this, &UCSDailySettlementSubsystem::HandleProductOrderPlaced);
		OrderSubsystem->OnFacilityOrderPlaced.AddUniqueDynamic(
			this, &UCSDailySettlementSubsystem::HandleFacilityOrderPlaced);
	}

	CustomerRuntimeSubsystem = InWorld.GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (IsValid(CustomerRuntimeSubsystem))
	{
		CustomerRuntimeSubsystem->OnShopliftEscaped.AddUniqueDynamic(
			this, &UCSDailySettlementSubsystem::HandleShopliftEscaped);
	}
}

void UCSDailySettlementSubsystem::HandleActorSpawned(AActor* SpawnedActor)
{
	BindTrashCan(Cast<ACSTrashCanActor>(SpawnedActor));
}

void UCSDailySettlementSubsystem::BindTrashCan(ACSTrashCanActor* TrashCan)
{
	// 같은 쓰레기통의 폐기 금액이 중복 집계되지 않게 한 번만 구독합니다.
	if (!IsValid(TrashCan) || BoundTrashCans.Contains(TrashCan))
	{
		return;
	}

	TrashCan->OnItemDisposed.AddUniqueDynamic(
		this, &UCSDailySettlementSubsystem::HandleItemDisposed);
	BoundTrashCans.Add(TrashCan);
}

void UCSDailySettlementSubsystem::HandleItemDisposed(
	AActor* DisposedItem,
	int32 TotalDisposedCount)
{
	// 쓰레기통의 누적 개수가 아니라 실제로 방금 폐기된 상품 한 개를 기록합니다.
	(void)TotalDisposedCount;

	// 낱개 상품만 집계하므로 배송 상자와 튜토리얼 쓰레기는 Cast 단계에서 제외됩니다.
	const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(DisposedItem);
	ACSStoreGameState* StoreGameState =
		GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (IsValid(Product) && IsValid(StoreGameState))
	{
		StoreGameState->RecordDisposedProduct(Product);
	}
}

void UCSDailySettlementSubsystem::HandleProductOrderPlaced(
	const FCSOrderResult& Result)
{
	ACSStoreGameState* StoreGameState =
		GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (Result.bSuccess && IsValid(StoreGameState))
	{
		StoreGameState->RecordOrderExpense(
			Result.TotalCost,
			false,
			Result.bImmediateDelivery);
	}
}

void UCSDailySettlementSubsystem::HandleFacilityOrderPlaced(
	const FCSFacilityOrderResult& Result)
{
	ACSStoreGameState* StoreGameState =
		GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (Result.bSuccess && IsValid(StoreGameState))
	{
		StoreGameState->RecordOrderExpense(
			Result.TotalCost,
			true,
			Result.bImmediateDelivery);
	}
}

void UCSDailySettlementSubsystem::HandleShopliftEscaped(
	ACSCustomerCharacter* Customer,
	int32 LostGold)
{
	(void)Customer;
	ACSStoreGameState* StoreGameState =
		GetWorld() ? GetWorld()->GetGameState<ACSStoreGameState>() : nullptr;
	if (IsValid(StoreGameState))
	{
		StoreGameState->RecordShopliftLoss(LostGold);
	}
}
