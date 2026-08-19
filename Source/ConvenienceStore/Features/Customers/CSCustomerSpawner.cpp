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
#include "Data/CSCustomerTrafficDataAsset.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Data/CSStorePhaseTypes.h"


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

	CachedStoreGameState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (IsValid(CachedStoreGameState))
	{
		CachedStoreGameState->OnScheduleEventFired.AddDynamic(this, &ACSCustomerSpawner::HandleScheduleEventFired);
		CachedStoreGameState->OnStorePhaseChanged.AddDynamic(this, &ACSCustomerSpawner::HandleStorePhaseChanged);
		CachedStoreGameState->OnStoreLevelChanged.AddDynamic(this, &ACSCustomerSpawner::HandleStoreLevelChanged);
	}

	if (!IsValid(TrafficDataAsset))
	{
		UE_LOG(LogTemp, Error,TEXT("[CSCustomerSpawner] TrafficDataAsset이 지정되지 않아 상점 레벨과 피크타임이 손님 생성에 반영되지 않습니다. %.1f초 간격 / 최대 %d명으로만 동작합니다."),
			SpawnInterval, MaxAliveCustomers);
	}

	// 구독만으로는 부족하고 여기서 현재 레벨을 한 번 읽어야 초기화 순서에 상관없이 맞는다.
	// 스포너의 BeginPlay가 ACSStoreGameMode::BeginPlay보다 먼저 돌면 아직 레벨이 확정되지 않았지만,
	// 그 경우엔 게임모드가 초기화하면서 OnStoreLevelChanged를 쏴 주므로 위 구독이 받아 준다.
	// 반대로 게임모드가 먼저 돌아 브로드캐스트를 놓쳤다면 여기서 읽는 값이 이미 올바른 레벨이다.
	ApplySpawnTimer();
}

void ACSCustomerSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(CachedStoreGameState))
	{
		CachedStoreGameState->OnScheduleEventFired.RemoveDynamic(this, &ACSCustomerSpawner::HandleScheduleEventFired);
		CachedStoreGameState->OnStorePhaseChanged.RemoveDynamic(this, &ACSCustomerSpawner::HandleStorePhaseChanged);
		CachedStoreGameState->OnStoreLevelChanged.RemoveDynamic(this, &ACSCustomerSpawner::HandleStoreLevelChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void ACSCustomerSpawner::HandleScheduleEventFired(FName EventId, FText Message)
{
	if (EventId == PeakStartEventId)
	{
		bIsPeakActive = true;
	}
	else if (EventId == PeakEndEventId)
	{
		bIsPeakActive = false;
	}
	else
	{
		// 배송 도착이나 손님 입장 시작 같은 나머지 이벤트는 생성량과 무관하다.
		return;
	}

	// 생성 간격이 바뀌었으니 다음 스폰부터 바로 반영되도록 타이머를 다시 건다.
	ApplySpawnTimer();

	UE_LOG(LogTemp, Log,
		TEXT("[CSCustomerSpawner] 피크타임 %s (상점 레벨 %d) - 생성 간격 %.2f초 / 동시 상한 %d명"),
		bIsPeakActive ? TEXT("시작") : TEXT("종료"),
		GetStoreLevel(),
		GetCurrentSpawnInterval(),
		GetCurrentMaxAlive());
}

void ACSCustomerSpawner::HandleStoreLevelChanged(int32 NewLevel)
{
	// 레벨이 오르면 생성 간격과 동시 인원 상한이 함께 바뀐다.
	ApplySpawnTimer();

	UE_LOG(LogTemp, Log,
		TEXT("[CSCustomerSpawner] 상점 레벨 %d - 생성 간격 %.2f초 / 동시 상한 %d명%s"),
		NewLevel,
		GetCurrentSpawnInterval(),
		GetCurrentMaxAlive(),
		bIsPeakActive ? TEXT(" (피크타임 적용 중)") : TEXT(""));
}

void ACSCustomerSpawner::HandleStorePhaseChanged(ECStorePhase NewPhase)
{
	// 영업 단계가 아니면 피크를 유지할 이유가 없다.
	// 피크 종료 시각을 24:00 이후로 옮기더라도 피크가 다음 날까지 켜진 채 넘어가지 않게 막는다.
	if (NewPhase != ECStorePhase::Open)
	{
		bIsPeakActive = false;
	}

	// 개점 시점에 맞춰 첫 손님까지의 대기 시간을 다시 잡는다.
	ApplySpawnTimer();
}

int32 ACSCustomerSpawner::GetStoreLevel() const
{
	if (!IsValid(CachedStoreGameState))
	{
		return 1;
	}

	return CachedStoreGameState->GetStoreLevel();
}

float ACSCustomerSpawner::GetCurrentSpawnInterval() const
{
	if (!IsValid(TrafficDataAsset))
	{
		// 설정이 없으면 기존 동작을 그대로 유지
		return FMath::Max(SpawnInterval, 0.1f);
	}

	return TrafficDataAsset->GetSpawnIntervalSeconds(GetStoreLevel(), bIsPeakActive);
}

int32 ACSCustomerSpawner::GetCurrentMaxAlive() const
{
	if (!IsValid(TrafficDataAsset))
	{
		return FMath::Max(MaxAliveCustomers, 1);
	}

	return TrafficDataAsset->GetMaxAliveCustomers(GetStoreLevel(), bIsPeakActive);
}

void ACSCustomerSpawner::ApplySpawnTimer()
{
	const int32 StoreLevel = GetStoreLevel();

	// 개점, 피크 전환, 레벨업처럼 조건이 바뀔 때만 불리는 함수
	if (IsValid(TrafficDataAsset) && !TrafficDataAsset->HasValuesForLevel(StoreLevel))
	{
		UE_LOG(LogTemp, Error,TEXT("[CSCustomerSpawner] 상점 레벨 %d에 해당하는 값이 %s에 없어 가장 가까운 레벨의 값으로 대체합니다."),
			StoreLevel, *TrafficDataAsset->GetName());
	}

	const float Interval = GetCurrentSpawnInterval();
	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle,
		this,
		&ACSCustomerSpawner::TrySpawnCustomer,//실행될 함수
		Interval, //반복 주기
		true,   // 반복
		Interval);  // 첫 스폰까지도 같은 간격만큼 대기
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

	if (TotalWeight <= 0.f) // 가중치가 전부 0이면 첫 종족으로 폴백
	{
		return Archetypes[0];
	}

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
	// GameState의 bCanSpawnCustomers 플래그를 게이트로 사용.
	// (준비 08:00~09:00 = 스폰 X, 영업 09:00~ = 스폰 O)
	// 이 값은 ACSStoreGameMode::MakePhaseData()가 단계에 따라 결정한다.

	// 영업 시간대(bCanSpawnCustomers)면 문이 닫혀 있어도 손님을 만든다.
	// 문이 닫혀 있는 동안에는 이들이 거리를 지나가기만 하고,
	// 열려 있으면 가게 앞에서 판정을 거쳐 매장에 들어온다.
	const ACSStoreGameState* StoreState = GetWorld()->GetGameState<ACSStoreGameState>();
	if (StoreState != nullptr && StoreState->bCanSpawnCustomers == false)
	{
		return;
	}

	const UCSCustomerRuntimeSubsystem* Subsystem =
		GetWorld()->GetSubsystem<UCSCustomerRuntimeSubsystem>();
	if (Subsystem != nullptr && Subsystem->IsBusinessHoursOver())
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

	//정리 후 현재 레벨(+피크)의 동시 인원 상한 이상이면 리턴(상한체크)
	// 고정값이 아니라 이 조회를 거쳐야 레벨별 상한과 피크타임 상한이 실제로 적용된다.
	if (SpawnedCustomers.Num() >= GetCurrentMaxAlive())
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


	// [한길] 손님을 만들기 전에 종족을 먼저 결정해야 종족별 크기를 스폰 순간부터 적용할 수 있습니다.
	UCSCustomerArchetypeDataAsset* SelectedArchetype = PickArchetypeBySpawnWeight();
	const float CharacterScale = SelectedArchetype != nullptr
		? FMath::Max(SelectedArchetype->CharacterScale, 0.1f)
		: 4.0f;

	// SpawnActorDeferred와 FinishSpawning 양쪽에 같은 값으로 넘긴다.
	// 스케일도 여기 담겨 있으므로 종족별 크기가 그대로 적용된다.
	const FTransform SpawnTransform(FRotator::ZeroRotator,SpawnLocation,FVector(CharacterScale));

	// =================================================================================
	// [초기화 순서: SpawnActorDeferred 사용 이유]
	// SpawnActor는 1) 액터/컴포넌트 생성 후 2) 즉시 BeginPlay()를 동기로 호출
	// 이 경우 스포너가 StorefrontLocation, Archetype 등의 초기값을 셋팅하기도 전에 손님의 StateTree가 시작됨
	// 그 결과, 아직 비어있는(ZeroVector) StorefrontLocation을 읽고 판정 지점 미배치로 오인해 개폐 여부와 무관하게 전원 입장하는 버그가 발생
	// 이를 방지하기 위해 SpawnActorDeferred를 사용하여 액터 메모리 할당만 하고 BeginPlay() 실행은 보류시킵니다.
	// =================================================================================
	ACSCustomerCharacter* Customer = GetWorld()->SpawnActorDeferred<ACSCustomerCharacter>(CustomerClass,SpawnTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

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

		// 가게 앞 판정 지점.
		// 비어 있으면 기본 지정했던 값 ZeroVector로 변경
		if (StorefrontPoint != nullptr)
		{
			State->StorefrontLocation = StorefrontPoint->GetActorLocation();
		}

	}

	// 스폰된 손님에게 종족(Archetype)을 확률적으로 배정
	// 여기부터 예산 + 가격 + 카테고리 판단이 실제 종족 데이터를 참조하게 됨
	UCSPurchaseDecisionComponent* Decision = Customer->FindComponentByClass<UCSPurchaseDecisionComponent>();
	if (Decision != nullptr)
	{
		Decision->Archetype = SelectedArchetype;

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

			// 이 손님의 종족 카드가 정해준 개수만큼 채워줌
			if (State != nullptr)
			{
				State->RemainingEvaluationBudget = Decision->Archetype->EvaluationBudget;
			}
		}
	}

	// =================================================================================
	// -필수!!- 새로운 초기값(예: 소지품, 기분 상태 등)을 세팅 할 일이 생긴다면, 무조건 FinishSpawning을 호출하기 전에 선언
	// [초기화 완료 및 시작]
	// 손님의 목적지, 종족, 시드, 빌런 여부, 외형 등 모든 셋팅이 끝
	// FinishSpawning을 호출하여 보류해두었던 BeginPlay()를 실행하고 StateTree를 실행
	// =================================================================================
	Customer->FinishSpawning(SpawnTransform);

	//스포너가 만든 손님들 중 레벨에 존재하고 있는 배열에 추가
	SpawnedCustomers.Add(Customer);

	//생성 확인용 디버그
	if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
            FString::Printf(TEXT("Spawn! Total: %d"), SpawnedCustomers.Num()));
    }
}
