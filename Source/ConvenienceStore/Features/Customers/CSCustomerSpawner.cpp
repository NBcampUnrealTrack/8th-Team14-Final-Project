// Fill out your copyright notice in the Description page of Project Settings.

#include "Features/Customers/CSCustomerSpawner.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "TimerManager.h"

ACSCustomerSpawner::ACSCustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACSCustomerSpawner::BeginPlay()
{
	Super::BeginPlay();

	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&ACSCustomerSpawner::TrySpawnCustomer,//실행될 함수
		SpawnInterval, //반복 주기
		true,   // 반복
		1.0f);  // 첫 스폰까지 1초 대기
}

void ACSCustomerSpawner::TrySpawnCustomer()
{
	//CustomerClass가 할당안되어있으면 즉시 중단
	if (CustomerClass == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CSCustomerSpawner] CustomerClass가 지정되지 않았습니다."));
		return;
	}

	// 하루 흐름 시스템 연동:
	// DT_StorePhase의 bCanSpawnCustomers 플래그를 게이트로 사용.
	// (준비 08:00~09:00 = 스폰 X, 영업 09:00~ = 스폰 O — 데이터 테이블이 결정)
	// TODO(확장): GameState의 OnScheduleEventFired를 구독해서
	//             PeakStart/PeakEnd에 SpawnInterval을 바꾸면 피크타임 웨이브 구현 가능.
	const ACSStoreGameState* StoreState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreState != nullptr && StoreState->bCanSpawnCustomers == false)
	{
		return;
	}

	// 퇴장한 손님은 목록에서 정리
	for (int32 i = SpawnedCustomers.Num() - 1; i >= 0; i--)
	{
		if (IsValid(SpawnedCustomers[i]) == false)
		{
			SpawnedCustomers.RemoveAt(i);
		}
	}

	//정리 후 MaxAliveCustomers 이상이면 리턴(상한체크)
	if (SpawnedCustomers.Num() >= MaxAliveCustomers)
	{
		return;
	}

	// EntrancePoint 액터(지점) 할당되어 있으면 할당된 위치, 없으면 스포너 자기 위치를 SpawnLocation으로 결정
	FVector SpawnLocation;
	if (EntrancePoint != nullptr)
	{
		SpawnLocation = EntrancePoint->GetActorLocation();
	}
	else
	{
		SpawnLocation = GetActorLocation();
	}


	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	//스폰 지점에 다른 액터와 겹쳐도 밀어내서라도 무조건 스폰되게 함 (스폰 실패로 조용히 스폰 안되는 상황 방지)


	ACSCustomerCharacter* Customer = GetWorld()->SpawnActor<ACSCustomerCharacter>(
		CustomerClass, SpawnLocation, FRotator::ZeroRotator, Params);
	//Params로 정한 규칙을 기준으로 생성

	// 생성 실패면 리턴
	if (Customer == nullptr)
	{
		return;
	}

	//생성된 손님의 GetCustomerState()로 상태 컴포넌트를 얻어서 ExitLocation에서 ExitPoint 있으면 그 위치, 없으면 스폰된 장소로 지정
	UCSCustomerStateComponent* State = Customer->GetCustomerState();
	if (State != nullptr)
	{
		if (ExitPoint != nullptr)
		{
			State->ExitLocation = ExitPoint->GetActorLocation();
		}
		else
		{
			State->ExitLocation = SpawnLocation;
		}
	}

	//스포너가 만든 손님들 중 레벨에 존재하고 있는 배열에 추가
	SpawnedCustomers.Add(Customer);

	//생성 확인용 디버그
	if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
            FString::Printf(TEXT("Spawn! Total: %d"), SpawnedCustomers.Num()));
    }
}
