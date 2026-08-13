// Fill out your copyright notice in the Description page of Project Settings.

#include "Features/Customers/CSCustomerSpawner.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "TimerManager.h"
#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/CSCustomerVisualDataAsset.h"
#include "Data/CSVillainTypes.h"

extern TAutoConsoleVariable<int32> CVarCS_FixedSeed;

ACSCustomerSpawner::ACSCustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACSCustomerSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 여기서 딱 한 번만 시드를 잡아야 이후 종족 배정 + 손님별 파생 시드가 전부 결정론적으로 나옴
	const int32 FixedSeed = CVarCS_FixedSeed.GetValueOnGameThread();
	MasterStream.Initialize(FixedSeed >= 0 ? FixedSeed : FMath::Rand());

	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&ACSCustomerSpawner::TrySpawnCustomer,//실행될 함수
		SpawnInterval, //반복 주기
		true,   // 반복
		1.0f);  // 첫 스폰까지 1초 대기
}

UCSCustomerArchetypeDataAsset* ACSCustomerSpawner::PickArchetypeBySpawnWeight() const
{
	if (Archetypes.Num() == 0) return nullptr;

	// 등록된 종족들의 스폰 가중치 합을 구함
	float TotalWeight = 0.f;
	for (const auto& Candidate : Archetypes)
	{
		if (Candidate != nullptr) TotalWeight += Candidate->SpawnWeightBase;
	}
	if (TotalWeight <= 0.f) return Archetypes[0]; // 가중치가 전부 0이면 첫 종족으로 폴백

	// 0~TotalWeight 사이 난수를 뽑아 가중치를 누적하며 그 난수를 처음 넘는 종족을 선택
	const float Roll = MasterStream.FRandRange(0.f, TotalWeight);

	float Cumulative = 0.f;
	for (const auto& Candidate : Archetypes)
	{
		if (Candidate == nullptr) continue;
		Cumulative += Candidate->SpawnWeightBase;
		if (Roll <= Cumulative) return Candidate;
	}
	return Archetypes.Last(); // 부동소수점 오차로 못 걸렸을 때 대비한 폴백
}

void ACSCustomerSpawner::ApplyCustomerVisual(ACSCustomerCharacter* Customer, const UCSCustomerVisualDataAsset* Visual) const
{
	if (Customer == nullptr || Visual == nullptr)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = Customer->GetMesh();
	if (MeshComp == nullptr)
	{
		return;
	}

	// 메시 교체
	// 종족마다 스켈레톤이 달라서 이 메시와 짝지어진 애니메이션이 아래에서 함께 적용되어야 정상적으로 재생됨
	if (Visual->CharacterMesh != nullptr)
	{
		MeshComp->SetSkeletalMesh(Visual->CharacterMesh);
	}

	// 애니메이션 블루프린트가 채워져 있으면 그걸 우선 사용
	// (나중에 ABP가 생기면 DA에 클래스만 채워 넣으면 코드 수정 없이 이 경로가 자동으로 활성화)
	if (Visual->AnimBlueprintClass != nullptr)
	{
		MeshComp->SetAnimInstanceClass(Visual->AnimBlueprintClass);
	}
	else if (Visual->FallbackWalkAnimation != nullptr)
	{
		// ABP가 없을 때 쓰는 간이 재생 방식
		MeshComp->PlayAnimation(Visual->FallbackWalkAnimation, /*bLooping=*/ true);
	}
}

ECSVillainAction ACSCustomerSpawner::RollVillainAction(const UCSCustomerArchetypeDataAsset* Archetype,
	FRandomStream& Stream) const
{
	//데이터가 없거나	빌런이 될 확률 0인 경우
	if (Archetype == nullptr || Archetype->VillainChance <= 0.0f)
	{
		return ECSVillainAction::None;
	}

	// 빌런이 되는지 안되는지 정해지는 곳
	if (Stream.FRand() >= Archetype->VillainChance)
	{
		return ECSVillainAction::None;
	}

	// 빌런 행동 중 무엇을 할지 고르기 위해, 각 행동의 비중(Weight)을  더해 총합을 구하는 로직
	// (PickArchetypeBySpawnWeight와 완전히 같은 누적 가중 방식)
	float TotalWeight = 0.0f;
	for (int32 i = 0; i < Archetype->VillainActionWeights.Num(); i++)
	{
		if (Archetype->VillainActionWeights[i].Action == ECSVillainAction::None)
		{
			continue;
		}
		TotalWeight += FMath::Max(Archetype->VillainActionWeights[i].Weight, 0.0f);
	}

	// 빌런으로 됬는데 데이터에 행동이 하나도 안 적혀 있거나 가중치 합이 0일 경우 발생하는 오류 방어 코드
	if (TotalWeight <= 0.0f)
	{
		return ECSVillainAction::None;
	}

	//총합 사이의 난수를 하나 뽑은 뒤, 항목들을 순회하며 가중치를 누적합니다. 누적값이 난수보다 커지는 순간의 행동을 선택
	const float Roll = Stream.FRandRange(0.0f, TotalWeight);

	float Cumulative = 0.0f;
	for (int32 i = 0; i < Archetype->VillainActionWeights.Num(); i++)
	{
		if (Archetype->VillainActionWeights[i].Action == ECSVillainAction::None)
		{
			continue;
		}

		Cumulative += FMath::Max(Archetype->VillainActionWeights[i].Weight, 0.0f);
		if (Roll <= Cumulative)
		{
			return Archetype->VillainActionWeights[i].Action;
		}
	}

	// 부동소수점 오차로 어느 구간에도 안 걸렸을 때의 폴백
	return Archetype->VillainActionWeights.Last().Action;
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
	if (StoreState != nullptr &&
		(StoreState->bCanSpawnCustomers == false || StoreState->bIsStoreOpen == false))
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

	// 스폰된 손님에게 종족(Archetype)을 확률적으로 배정
	// 여기부터 예산 + 가격 + 카테고리 판단이 실제 종족 데이터를 참조하게 됨
	UCSPurchaseDecisionComponent* Decision = Customer->FindComponentByClass<UCSPurchaseDecisionComponent>();
	if (Decision != nullptr)
	{
		Decision->Archetype = PickArchetypeBySpawnWeight();

		// 이 손님 전용 파생 시드 주입 마스터 스트림에서 하나 뽑아 넘기면
		// 이 손님의 예산 추첨/구매 판단 굴림이 전부 이 시드로만 결정됨
		const int32 CustomerSeed = MasterStream.RandHelper(MAX_int32);
		Decision->InitializeRandomStream(CustomerSeed);

		// [빌런 판정 로직] (추후에 맵에 대해서 빌런 확률 더 증가한다 이런게 추가되면 값을 받아와서 계산하는 로직을 추가)
		// 빌런 판정용 파생 시드 하나 더 발급
		const int32 VillainSeed = MasterStream.RandHelper(MAX_int32);
		FRandomStream VillainStream(VillainSeed);

		// 빌런 행동 추첨
		// 결과가 일반 손님이면 ECSVillainAction::None이 반환
		const ECSVillainAction VillainAction = RollVillainAction(Decision->Archetype, VillainStream);

		// 디버그 로직 (빌런 생성될때)
		if (State != nullptr && VillainAction != ECSVillainAction::None)
		{
			State->AssignVillainAction(VillainAction);

			UE_LOG(LogTemp, Warning, TEXT("[빌런배정] %s | 종족=%s | 행동=%s"),
			   *Customer->GetName(),
			   Decision->Archetype ? *Decision->Archetype->DisplayName.ToString() : TEXT("미배정"),
			   *UEnum::GetDisplayValueAsText(VillainAction).ToString());
		}

		// =============

		if (Decision->Archetype != nullptr)
		{
			ApplyCustomerVisual(Customer, Decision->Archetype->Visual);
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
