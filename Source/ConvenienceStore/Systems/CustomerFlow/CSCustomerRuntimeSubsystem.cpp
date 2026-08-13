// Fill out your copyright notice in the Description page of Project Settings.


#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/Interface/CSCustomerCounterInterface.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Data/CSFreshnessSettingsDataAsset.h"
#include "Data/CSProductTypes.h"
#include "EngineUtils.h"
#include "Components/ArrowComponent.h"
// 아래 3개는 팀원 파일에 따라 수정해야함
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"

TAutoConsoleVariable<int32> CVarCS_FixedSeed(
	TEXT("cs.Seed"),
	-1,
	TEXT("0 이상이면 이 값을 손님 관련 굴림(종족 배정/예산 추첨/진열대 선택)의 고정 시드로 사용. -1(기본값)이면 매번 다른 시드"),
	ECVF_Default);

void UCSCustomerRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 이 서브시스템이 살아있는 동안 한 번만 시드를 잡음
	const int32 FixedSeed = CVarCS_FixedSeed.GetValueOnGameThread();
	ShelfRandomStream.Initialize(FixedSeed >= 0 ? FixedSeed : FMath::Rand());

	RefreshShelves();//레벨 시작할때 레벨에 있는 진열대를 바로 등록 (만약 타이쿤 시스템 도입시 생각)
}

void UCSCustomerRuntimeSubsystem::RefreshShelves()
{
	Shelves.Reset();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 진열대가 스스로 등록하는 방식이 아니라 여기서 찾아서 수집한다.
	for (TActorIterator<ACSShelfActor> It(World); It; ++It)
	{
		ACSShelfActor* Shelf = *It;
		if (Shelf != nullptr)
		{
			Shelves.Add(Shelf);
		}
	}
}

const TArray<TObjectPtr<AActor>>& UCSCustomerRuntimeSubsystem::GetShelves() const
{
	return Shelves;
}

FVector UCSCustomerRuntimeSubsystem::GetShelfBrowseLocation(const AActor* ShelfActor, const float BrowseDistance) const
{
	if (ShelfActor == nullptr)
	{
		return FVector::ZeroVector;
	}

	//진열대 위치 + 진열대 정면방향 × BrowseDistance 를 계산해 반환. 손님이 그 진열대 앞에 서는 좌표반환
	//EQS나 진열대에서 서 있는 위치를 알려줄 수 있으면 BrowseDistance 삭제 가능
	return ShelfActor->GetActorLocation() + ShelfActor->GetActorForwardVector() * BrowseDistance;
}

// [임시/테스트용]
AActor* UCSCustomerRuntimeSubsystem::PickRandomUnvisitedShelf(const TArray<TObjectPtr<AActor>>& Visited) const
{
	//방문하지 않은 진열대 배열
	TArray<AActor*> Candidates;

	for (int32 i = 0; i < Shelves.Num(); i++)
	{
		AActor* Shelf = Shelves[i];
		if (Shelf == nullptr)
		{
			continue;
		}

		// 현재 선반이 이미 방문한 목록(Visited)에 포함되어 있는지 확인
		bool bVisited = false;
		for (int32 j = 0; j < Visited.Num(); j++)
		{
			if (Visited[j] == Shelf)
			{
				bVisited = true;
				break;
			}
		}

		// 방문한 적이 없는 진열대라면 후보군에 추가
		if (bVisited == false)
		{
			Candidates.Add(Shelf);
		}
	}

	// 조건에 맞는 진열대가 하나도 없다면 nullptr 반환
	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	// 후보군 배열 내에서 무작위 인덱스를 하나 뽑아 해당 진열대를 반환
	const int32 PickIndex = ShelfRandomStream.RandRange(0, Candidates.Num() - 1);
	return Candidates[PickIndex];
}

// [임시/테스트용]
bool UCSCustomerRuntimeSubsystem::HasUnvisitedShelf(const TArray<TObjectPtr<AActor>>& Visited) const
{
	for (int32 i = 0; i < Shelves.Num(); i++)
	{
		AActor* Shelf = Shelves[i];
		if (Shelf == nullptr)
		{
			continue;
		}

		bool bVisited = false;
		for (int32 j = 0; j < Visited.Num(); j++)
		{
			if (Visited[j] == Shelf)
			{
				bVisited = true;
				break;
			}
		}

		// 방문하지 않은 진열대를 하나라도 발견하면 즉시 true를 반환합니다.
		if (bVisited == false)
		{
			return true;
		}
	}
	// 모든 진열대를 순회했으나 전부 Visited에 포함되어 있다면 false를 반환합니다.
	return false;
}

TArray<AActor*> UCSCustomerRuntimeSubsystem::GetProductsOnShelf(const AActor* ShelfActor) const
{
	TArray<AActor*> Result;

	if (ShelfActor == nullptr)
	{
		return Result;
	}

	// 팀원 설계: TryPlaceItem이 상품을 진열대에 Attach함 → Attach된 상품 = 진열된 재고
	// 진열대 액터에 부착(Attached)된 모든 자식 액터를 가져옴
	TArray<AActor*> Attached;
	ShelfActor->GetAttachedActors(Attached);

	// 부착된 액터들 중 상품인 것만 캐스팅하여 필터링
	for (int32 i = 0; i < Attached.Num(); i++)
	{
		ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Attached[i]);
		if (Product != nullptr && Product->IsSellable())
		{
			Result.Add(Product);
		}
	}

	return Result;
}

TArray<FCSPurchaseCandidate> UCSCustomerRuntimeSubsystem::BuildPurchaseCandidates(const AActor* ShelfActor) const
{
	TArray<FCSPurchaseCandidate> Candidates;
	// 기존 GetProductsOnShelf()를 그대로 재사용해서 진열된 상품 액터들을 가져옴
	TArray<AActor*> Products = GetProductsOnShelf(ShelfActor);

	for (AActor* ProductActor : Products)
	{
		ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
		if (Product == nullptr) continue;  // 상품이 아닌 액터 방어

		// TakeProduct()가 쓰는 것과 동일한 GetProductRow()로 상품 마스터 데이터 참조를 가져옴
		FCSPurchaseCandidate Candidate;
		Candidate.ProductRowHandle = Product->GetProductRow();
		Candidate.ProductActor = ProductActor;
		Candidates.Add(Candidate);
	}
	return Candidates;
}

bool UCSCustomerRuntimeSubsystem::HasShelfStock(const AActor* ShelfActor) const
{
	return GetProductsOnShelf(ShelfActor).Num() > 0;
}

FName UCSCustomerRuntimeSubsystem::TakeProduct(AActor* ShelfActor, AActor* ProductActor, ACSCustomerCharacter* Customer)
{
	ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor);
	if (ShelfActor == nullptr || Product == nullptr || Customer == nullptr)
	{
		return NAME_None;
	}

	if (!Product->IsSellable())
	{
		return NAME_None;
	}

	// ProductId는 DT_ProductMaster에서 ProductRow가 가리키는 RowName입니다.
	const FName ProductId = Product->GetProductId();
	if (ProductId.IsNone())
	{
		return NAME_None;
	}

	UCSCustomerStateComponent* CustomerState = Customer->GetCustomerState();
	if (CustomerState == nullptr)
	{
		return NAME_None;
	}

	const FDataTableRowHandle ProductRow = Product->GetProductRow();
	const FCSProductDataRow* ProductData =
		ProductRow.GetRow<FCSProductDataRow>(TEXT("UCSCustomerRuntimeSubsystem::TakeProduct"));
	if (ProductData == nullptr)
	{
		return NAME_None;
	}

	const UCSStorePricingSubsystem* PricingSubsystem =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<UCSStorePricingSubsystem>()
			: nullptr;
	const int32 SalePriceAtPickup = IsValid(PricingSubsystem)
		? PricingSubsystem->GetSalePrice(ProductRow)
		: ProductData->RecommendedSalePrice;

	// 진열대의 슬롯 점유를 해제합니다. (플레이어가 진열된 물건을 다시 집을 때와 동일한 규약 유지)
	// 이 처리를 누락하면 물리적 상품 액터는 사라져도, 논리적으로 슬롯이 차 있다고 인식되어 재진열이 불가해집니다.
	// 이 액터가 진열대 규약을 구현했으면, 그 진열대한테 '이 상품 치웠으니 슬롯 비워' 라고 알리는 기능 가져옴
	if (ShelfActor->Implements<UCSShelfPlaceable>())
	{
		ICSShelfPlaceable::Execute_RemovePlacedItem(ShelfActor, Product);
	}

	FCSProductRuntimeState CustomerProductRuntimeState =
		Product->GetProductRuntimeState();
	UWorld* World = GetWorld();
	const ACSStoreGameState* StoreGameState =
		IsValid(World) ? World->GetGameState<ACSStoreGameState>() : nullptr;
	const UCSFreshnessSettingsDataAsset* FreshnessSettings =
		IsValid(StoreGameState) ? StoreGameState->GetFreshnessSettings() : nullptr;
	if (IsValid(World) && IsValid(FreshnessSettings))
	{
		CustomerProductRuntimeState.StopFreshnessDecay(
			World->GetTimeSeconds(),
			FreshnessSettings->DecayIntervalSeconds,
			FreshnessSettings->DecayAmountPerInterval);
	}

	// ProductRow(테이블+행)와 클래스를 함께 남겨야 나중에 원본 그대로 되돌릴 수 있다.
	// 액터를 파괴하기 전에 기록
	CustomerState->AddSelectProduct(
		ProductRow,
		Product->GetClass(),
		CustomerProductRuntimeState,
		SalePriceAtPickup);

	// 실제 지출 금액 기록
	CustomerState->RecordSpending(SalePriceAtPickup);

	if (CustomerState->SelectProducts.Num() > 0)
	{
		// 실제 상품을 제거하기 전에 같은 데이터로 장바구니 표시용 상품을 추가합니다.
		Customer->AddBasketDisplayProduct(CustomerState->SelectProducts.Last());
	}

	// 손님이 가져간 상품 액터를 월드에서 물리적으로 완전히 제거합니다.
	// (상품 판매 및 인벤토리 처리는 델리게이트를 통해 외부에서 처리하도록 분리)
	Product->Destroy();

	// 상품 획득 이벤트를 브로드캐스트하여 통계, 재고 등 외부 시스템에 알립니다.
	OnItemTaken.Broadcast(Customer, ProductId);
	return ProductId;
}

void UCSCustomerRuntimeSubsystem::RegisterCounter(AActor* CounterActor)
{
	// 계산대 등록. ICSCustomerCounterInterface를 구현하지 않은 액터는 등록을 거부
	// (등록되면 GetQueueSlotLocation이 이 액터에게 줄 좌표를 위임함)
	if (CounterActor != nullptr && Cast<ICSCustomerCounterInterface>(CounterActor) == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSCustomerRuntimeSubsystem] %s 는 ICSCustomerCounterInterface를 구현하지 않아 등록 불가."),
			*CounterActor->GetName());
		return;
	}

	Counter = CounterActor;
}

void UCSCustomerRuntimeSubsystem::UnregisterCounter(AActor* CounterActor)
{
	// 다른 계산대가 등록된 상태에서 잘못 해제되지 않도록 현재 등록된 액터와 일치하는지 확인합니다.
	if (Counter == CounterActor)
	{
		Counter = nullptr;
	}
}

int32 UCSCustomerRuntimeSubsystem::JoinQueue(ACSCustomerCharacter* Customer)
{
	if (Customer == nullptr)
	{
		return -1;
	}

	// 이미 대기열에 포함되어 있는 손님이라면 중복 추가를 방지하고 기존 인덱스 반환
	const int32 ExistingIndex = GetQueueIndex(Customer);
	if (ExistingIndex != -1)
	{
		return ExistingIndex;
	}

	// 대기열 맨 끝에 손님을 추가하고, 해당 인덱스를 반환합니다.
	Queue.Add(Customer);
	return Queue.Num() - 1;
}

void UCSCustomerRuntimeSubsystem::LeaveQueue(ACSCustomerCharacter* Customer)
{
	Queue.Remove(Customer); // 배열 중간에서 삭제되더라도 TArray가 자동으로 빈 공간을 당겨주고 뒷 순번도 같이 갱신됨
}

int32 UCSCustomerRuntimeSubsystem::GetQueueIndex(const ACSCustomerCharacter* Customer) const
{
	// 순차 검색을 통해 손님의 위치를 찾습니다.
	for (int32 i = 0; i < Queue.Num(); i++)
	{
		if (Queue[i] == Customer)
		{
			return i;
		}
	}
	return -1;

}

FVector UCSCustomerRuntimeSubsystem::GetQueueSlotLocation(const int32 Index) const
{
	const ICSCustomerCounterInterface* CounterInterface = Cast<ICSCustomerCounterInterface>(Counter);
	if (CounterInterface == nullptr || Index < 0)
	{
		return FVector::ZeroVector;
	}

	// 대기열의 위치 계산 로직 자체는 계산대 액터가 가지고 있으므로, 인터페이스 함수를 호출하여 좌표를 받아옵니다.
	return CounterInterface->GetQueueSlotLocation(Index);
}

ACSCustomerCharacter* UCSCustomerRuntimeSubsystem::GetFrontCustomer() const
{
	// 대기열의 가장 첫 번째(계산 차례인) 손님을 반환
	if (Queue.Num() == 0)
	{
		return nullptr;
	}
	return Queue[0];
}

void UCSCustomerRuntimeSubsystem::CompleteFrontCheckout()
{
	if (Queue.Num() == 0)
	{
		return;
	}

	ACSCustomerCharacter* Front = Queue[0];

	// 첫 번째 요소를 제거합니다. (TArray 특성상 뒤에 있던 원소들이 자동으로 한 칸씩 당겨짐)
	Queue.RemoveAt(0);

	if (Front != nullptr)
	{
		UCSCustomerStateComponent* State = Front->GetCustomerState();
		if (State != nullptr)
		{
			// 손님의 상태 컴포넌트에 계산이 완료되었음을 마킹
			State->bCheckoutDone = true;
		}
		// 결제가 완료된 상품은 고객 장바구니에서도 즉시 제거합니다.
		Front->ClearBasketDisplayProducts();

		// 계산이 완료되었음을 브로드캐스트하여, UI 갱신이나 매출 증가 등의 후속 시스템 처리 유도
		OnCheckoutCompleted.Broadcast(Front);
	}
}

float UCSCustomerRuntimeSubsystem::GetPatienceSeconds(const ACSCustomerCharacter* Customer) const
{
	// ["백록담"] (소비심리 파트): 손님 따라 다른 값을 반환하도록 교체. 호출하는 WaitInQueue 태스크는 수정할 필요가 없음
	return DefaultPatienceSeconds;
}

void UCSCustomerRuntimeSubsystem::AbandonCheckout(ACSCustomerCharacter* Customer, const float WaitedSeconds)
{

	if (Customer == nullptr)
	{
		return;
	}

	// 대기열에서 먼저 제거
	LeaveQueue(Customer);

	//  들고 있던 상품을 떨어뜨린다. (진열대에 되돌려놓는 기능이 들어오면 이 호출은 그쪽 Task로 이동)
	//DropCustomerItems(Customer);

	// 다시 줄 서지 않도록 true로 지정
	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State != nullptr)
	{
		State->bCheckoutAbandoned = true;
	}

	//    진열대에 되돌려놓을지, 그 자리에 버릴지 결정
	//    되돌려놓기로 하면 상품 목록을 그대로 두고, ReturnItems 상태가 처리한다.
	//    [임시] 확률은 나중에 소비심리 성향값으로 교체 가능한 자리.
	const bool bWillReturnToShelf = (ShelfRandomStream.FRand() < GetReturnToShelfChance(Customer));

	if (bWillReturnToShelf == false)
	{
		DropCustomerItems(Customer);
	}

	//계산을 포기했다고 방송을 전파
	OnCheckoutAbandoned.Broadcast(Customer, WaitedSeconds);

	UE_LOG(LogTemp, Log,TEXT("[CSCustomerRuntimeSubsystem] %s 가 %.1f초 대기 후 계산을 포기했습니다."),*Customer->GetName(), WaitedSeconds);
}

void UCSCustomerRuntimeSubsystem::NotifyCustomerDespawned(ACSCustomerCharacter* Customer)
{
	const UCSCustomerStateComponent* CustomerState = IsValid(Customer)
		? Customer->GetCustomerState()
		: nullptr;

	// CustomerState가 없으면(비정상 상황) 빈손으로 취급
	const bool bCheckoutDone = IsValid(CustomerState) && CustomerState->bCheckoutDone;

	OnCustomerDespawned.Broadcast(Customer, bCheckoutDone);
}

int32 UCSCustomerRuntimeSubsystem::DropCustomerItems(ACSCustomerCharacter* Customer)
{
		if (Customer == nullptr)
	{
		return 0;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr)
	{
		return 0;
	}

	const int32 ItemCount = State->SelectProducts.Num();
	if (ItemCount == 0)
	{
		return 0;
	}

	// 좌우 두 갈래로 나눠 쌓는다. 인덱스 0 = 왼쪽, 1 = 오른쪽.
	// 각 갈래는 자기 화살표를 기준으로 독립적으로 열을 늘려간다.
	float ColumnTopZ[2] = { 0.0f, 0.0f };        // 그 열에서 다음 상품을 놓을 높이 (기준점 기준)
	int32 ColumnIndex[2] = { 0, 0 };             // 그 갈래에서 지금 몇 번째 열인지
	float MaxItemRadius[2] = { 0.0f, 0.0f };     // 그 갈래에서 본 가장 큰 상품 반경

	int32 SpawnedCount = 0;

	for (int32 i = 0; i < ItemCount; i++)
	{
		// 좌우 번갈아 배정해서 양쪽이 고르게 쌓이게 한다.
		const int32 SideIndex = i % 2;

		// 시작 위치는 BP에서 지정한 화살표가 알려준다.
		// 종족마다 키와 팔 위치가 달라서 코드에 숫자로 박지 않는다.
		const UArrowComponent* Origin = Customer->GetDropOrigin(SideIndex);
		if (Origin == nullptr)
		{
			continue;
		}

		// 열 간격은 그 갈래에서 본 가장 큰 상품 지름 기준으로 잡는다.
		// 고정값을 쓰면 큰 상품이 섞였을 때 옆 열과 겹친다.
		const float ColumnSpacing = FMath::Max(
			DropMinColumnSpacing,
			MaxItemRadius[SideIndex] * 2.0f + DropColumnGap);

		// 열이 넘치면 화살표 정면 방향으로 새 열을 만든다.
		const FVector ColumnBase =
			Origin->GetComponentLocation() +
			Origin->GetForwardVector() * (ColumnSpacing * static_cast<float>(ColumnIndex[SideIndex]));

		// 완전히 정렬돼 보이지 않게 아주 살짝 흔들어준다.
		const FVector Jitter(
			FMath::FRandRange(-DropJitterRadius, DropJitterRadius),
			FMath::FRandRange(-DropJitterRadius, DropJitterRadius),
			0.0f);

		const FVector SpawnLocation =
			ColumnBase + Jitter + FVector(0.0f, 0.0f, ColumnTopZ[SideIndex]);

		// 전부 같은 방향을 보고 있으면 인위적으로 보이므로 Yaw만 랜덤하게 준다.
		const FRotator RandomRotation(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);

		const FTransform SpawnTransform(RandomRotation, SpawnLocation);

		AActor* Spawned = RespawnSelectProduct(State->SelectProducts[i], SpawnTransform);
		if (Spawned == nullptr)
		{
			continue;
		}

		SpawnedCount++;

		// 실제 크기를 재서 다음 상품의 높이와 열 간격을 정한다.
		// 상품마다 크기가 달라 고정값으로는 겹치거나 공중에 뜬다.
		const FVector Extent = Spawned->GetComponentsBoundingBox(false).GetExtent();
		const float ItemHeight = Extent.Z * 2.0f;
		const float ItemRadius = FMath::Max(Extent.X, Extent.Y);

		if (ItemRadius > MaxItemRadius[SideIndex])
		{
			MaxItemRadius[SideIndex] = ItemRadius;
		}

		ColumnTopZ[SideIndex] += ItemHeight + DropStackGap;

		// 팔 높이를 넘으면 같은 쪽에 새 열을 만들고 다시 아래부터 쌓는다.
		if (ColumnTopZ[SideIndex] >= DropMaxStackHeight)
		{
			ColumnIndex[SideIndex]++;
			ColumnTopZ[SideIndex] = 0.0f;
		}
	}

	// 일부 생성에 실패해도 목록은 비운다.
	// 남겨두면 손님이 계속 "상품을 들고 있는" 상태가 되어 이후 판정이 꼬인다.
	State->ClearSelectItems();
	Customer->ClearBasketDisplayProducts();

	return SpawnedCount;

}

ACSProductPickupActor* UCSCustomerRuntimeSubsystem::RespawnSelectProduct(const FCSSelectProduct& Selected,const FTransform& SpawnTransform, const bool bEnableDropPhysics)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Selected.ProductClass == nullptr)
	{
		return nullptr;
	}

	// 배송 상자(ACSDeliveryCrateActor::SpawnProductActor)와 동일한 패턴을 쓴다.
	// SpawnActorDeferred는 BeginPlay 전에 데이터를 세팅할 수 있어서,
	// 상품이 초기화 전 상태로 한 프레임 존재하는 상황을 막는다.
	ACSProductPickupActor* Product = World->SpawnActorDeferred<ACSProductPickupActor>(
		Selected.ProductClass,
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

	if (!IsValid(Product))
	{
		return nullptr;
	}

	// 기록해둔 ProductRow로 초기화하면 내부에서 DataAsset 기반 메시와 스케일까지 적용된다.
	if (!Product->InitializeProduct(Selected.ProductRow))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSCustomerRuntimeSubsystem] 상품 초기화 실패로 되돌리지 못했습니다: %s"),
			*Selected.ProductRow.RowName.ToString());
		Product->Destroy();
		return nullptr;
	}

	Product->ApplyProductRuntimeState(Selected.ProductRuntimeState);
	Product->FinishSpawning(SpawnTransform);
	if (!IsValid(Product))
	{
		return nullptr;
	}

	// 플레이어가 바닥에 내려놓을 때와 동일하게 처리한다.
	// 콜리전을 켜야 다시 집을 수 있고, 물리를 켜야 자연스럽게 안착한다.
	Product->SetActorEnableCollision(true);

	// 진열대에 놓을 상품은 물리 기능 X
	if (bEnableDropPhysics)
	{
		Product->EnableWorldDropPhysics();
	}

	return Product;
}

float UCSCustomerRuntimeSubsystem::GetReturnToShelfChance(const ACSCustomerCharacter* Customer) const
{
	// TODO(소비심리 파트): 손님 소비심리에 따라 다른 값을 반환하도록 교체
	return DefaultReturnToShelfChance;
}

AActor* UCSCustomerRuntimeSubsystem::PickRandomShelf() const
{
	TArray<AActor*> Candidates;

	for (int32 i = 0; i < Shelves.Num(); i++)
	{
		AActor* Shelf = Shelves[i];
		if (Shelf == nullptr)
		{
			continue;
		}
		//만약 빈진열대나 원하는 진열대를 선택하고 싶으면 여기서 조건을 달아주면 된다.
		Candidates.Add(Shelf);
	}

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	const int32 PickIndex = ShelfRandomStream.RandRange(0, Candidates.Num() - 1);
	return Candidates[PickIndex];
}

int32 UCSCustomerRuntimeSubsystem::ReturnCustomerItemsToShelf(ACSCustomerCharacter* Customer, AActor* ShelfActor)
{
	if (Customer == nullptr || ShelfActor == nullptr)
	{
		return 0;
	}

	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State == nullptr || State->SelectProducts.Num() == 0)
	{
		return 0;
	}

	if (ShelfActor->Implements<UCSShelfPlaceable>() == false)
	{
		// 진열대가 아니면 그냥 그 자리에 떨군다.
		DropCustomerItems(Customer);
		return 0;
	}

	const FVector ShelfLocation = ShelfActor->GetActorLocation();
	int32 PlacedCount = 0;

	// 슬롯에 못 들어간 상품은 따로 모아뒀다가 마지막에 떨어뜨림
	TArray<FCSSelectProduct> LeftoverProducts;

	for (int32 i = 0; i < State->SelectProducts.Num(); i++)
	{
		// 진열대에 놓을 상품은 물리를 끈 상태로 생성
		ACSProductPickupActor* Product =
			RespawnSelectProduct(State->SelectProducts[i], FTransform(ShelfLocation), false);

		if (Product == nullptr)
		{
			continue;
		}

		// 진열대가 빈 슬롯을 찾아 Attach하고 슬롯 점유까지 처리한다.
		// 빈 슬롯이 없으면 false를 반환하므로 슬롯 초과 배치는 구조적으로 불가능
		if (ICSShelfPlaceable::Execute_TryPlaceItem(ShelfActor, Product, ShelfLocation)) //손님 AI가 반환하는 물건들은 진열대의 원점과 가까운 빈 슬롯부터 채움
		{
			PlacedCount++;
			continue;
		}

		// 슬롯이 꽉 찼다. 방금 만든 액터는 지우고 나중에 떨어뜨릴 목록에 추가
		LeftoverProducts.Add(State->SelectProducts[i]);
		Product->Destroy();
	}

	// 슬롯에 들어간 상품은 목록에서 지우고, 남은 것만 다시 채워 넣음
	State->ClearSelectItems();
	Customer->ClearBasketDisplayProducts();
	for (int32 i = 0; i < LeftoverProducts.Num(); i++)
	{
		State->SelectProducts.Add(LeftoverProducts[i]);
	}

	// 남은 상품이 있으면 그 자리에 떨어뜨린다. (내부에서 목록을 다시 비운다)
	if (State->SelectProducts.Num() > 0)
	{
		DropCustomerItems(Customer);
	}

	return PlacedCount;

}


