// Fill out your copyright notice in the Description page of Project Settings.


#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Features/Customers/Interface/CSCustomerCounterInterface.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "EngineUtils.h"
// 아래 3개는 팀원 파일에 따라 수정해야함
#include "Features/Shelves/CSShelfActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Interaction/CSShelfPlaceable.h"

void UCSCustomerRuntimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
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
	const int32 PickIndex = FMath::RandRange(0, Candidates.Num() - 1);
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
		if (Product != nullptr)
		{
			Result.Add(Product);
		}
	}

	return Result;
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

	// 진열대의 슬롯 점유를 해제합니다. (플레이어가 진열된 물건을 다시 집을 때와 동일한 규약 유지)
	// 이 처리를 누락하면 물리적 상품 액터는 사라져도, 논리적으로 슬롯이 차 있다고 인식되어 재진열이 불가해집니다.
	// 이 액터가 진열대 규약을 구현했으면, 그 진열대한테 '이 상품 치웠으니 슬롯 비워' 라고 알리는 기능 가져옴
	if (ShelfActor->Implements<UCSShelfPlaceable>())
	{
		ICSShelfPlaceable::Execute_RemovePlacedItem(ShelfActor, Product);
	}

	// ProductRow(테이블+행)와 클래스를 함께 남겨야 나중에 원본 그대로 되돌릴 수 있다.
	// 액터를 파괴하기 전에 기록
	CustomerState->AddSelectProduct(Product->GetProductRow(), Product->GetClass());

	// 손님이 가져간 상품 액터를 월드에서 물리적으로 완전히 제거합니다.
	// (상품 판매 및 인벤토리 처리는 델리게이트를 통해 외부에서 처리하도록 분리)
	Product->Destroy();

	// 상품 획득 이벤트를 브로드캐스트하여 통계, 재고 등 외부 시스템에 알립니다.
	OnItemTaken.Broadcast(Customer, ProductId);
	return ProductId;
}

// [임시/테스트용]
FName UCSCustomerRuntimeSubsystem::TakeRandomProduct(AActor* ShelfActor, ACSCustomerCharacter* Customer)
{
	// 현재 진열대에 있는 모든 상품 목록을 가져옵니다.
	TArray<AActor*> Products = GetProductsOnShelf(ShelfActor);

	if (Products.Num() == 0)
	{
		return NAME_None; // 재고가 없으면 아무것도 하지 않음
	}

	// 목록 중 무작위 인덱스를 뽑아 상품 획득(TakeProduct) 로직으로 넘깁니다.
	// Task에서 확률적으로 TakeRandomProduct가 부르게 할 예정
	const int32 PickIndex = FMath::RandRange(0, Products.Num() - 1);
	return TakeProduct(ShelfActor, Products[PickIndex], Customer);

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

		// 계산이 완료되었음을 브로드캐스트하여, UI 갱신이나 매출 증가 등의 후속 시스템 처리 유도
		OnCheckoutCompleted.Broadcast(Front);
	}
}

