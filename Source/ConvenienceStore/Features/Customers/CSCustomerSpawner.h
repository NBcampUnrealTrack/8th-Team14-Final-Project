// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Data/CSProductTypes.h"   // ECSReputationGrade
#include "CSCustomerSpawner.generated.h"

class ACSCustomerCharacter;
class ACSStoreGameState;
class UCSCustomerArchetypeDataAsset;
class UCSCustomerTrafficDataAsset;
class UCSCustomerVisualDataAsset;
enum class ECSVillainAction : uint8;

UCLASS()
class CONVENIENCESTORE_API ACSCustomerSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACSCustomerSpawner();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void TrySpawnCustomer();

	// 피크타임 시작/종료는 그 시각을 지나가는 순간의 신호라 구독으로만 알 수 있음
	UFUNCTION()
	void HandleScheduleEventFired(FName EventId, FText Message);

	// 영업 단계를 벗어나면 피크 상태를 되돌려 다음 날로 새지 않게 함
	UFUNCTION()
	void HandleStorePhaseChanged(ECStorePhase NewPhase);

	// 스폰할 손님 클래스 (BP_CSCustomer 지정)
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TSubclassOf<ACSCustomerCharacter> CustomerClass;

	// 평판 등급별 생성 간격과 동시 인원, 피크타임 배율을 담은 설정
	// 비워 두면 오류 로그를 남기고 아래 SpawnInterval / MaxAliveCustomers로만 동작함
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TObjectPtr<UCSCustomerTrafficDataAsset> TrafficDataAsset;

	// DT_StoreScheduleEvent에서 피크 시작과 종료로 쓸 행 이름
	// 데이터 테이블의 행 이름을 바꾸면 이 값도 함께 바꿔야 함
	UPROPERTY(EditAnywhere, Category = "Spawn")
	FName PeakStartEventId = TEXT("LunchPeakStart");

	UPROPERTY(EditAnywhere, Category = "Spawn")
	FName PeakEndEventId = TEXT("LunchPeakEnd");

	// TrafficDataAsset이 없을 때만 쓰는 손님 생성 간격
    UPROPERTY(EditAnywhere, Category = "Spawn")
    float SpawnInterval = 5.0f;

	// TrafficDataAsset이 없을 때만 쓰는 동시 손님 수 상한
    UPROPERTY(EditAnywhere, Category = "Spawn")
    int32 MaxAliveCustomers = 5;

	// 거리 양쪽 끝 지점A.
	//
	// 손님마다 A→B 또는 B→A 중 하나를 뽑아 걷습니다.
	// 이름이 Entrance/Exit가 아닌 이유: 양방향이라 어느 쪽이 시작인지 손님마다 다릅니다.
	//
	// 크기가 있는 액터(TriggerBox 등)를 넣으면 그 영역 안에서 임의 지점을 고르고,
	// 빈 Actor를 넣으면 그 점 하나만 씁니다.
	// 할당 안 되어 있으면 스포너 자신의 위치에서 생성됩니다.
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TObjectPtr<AActor> StreetEndpointA;

	// 거리 양쪽 끝 지점B. 위와 같습니다.
	// 비워두면 시작 지점과 동일해져 제자리에서 소멸합니다.
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TObjectPtr<AActor> StreetEndpointB;

	// 가게 문 앞 판정 지점.
	// 영업 중이면 매장에 들어가고, 아니면 ExitPoint로 계속 걸어갑니다.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TObjectPtr<AActor> StorefrontPoint;


	// 스폰 시 손님에게 확률적으로 배정할 종족 데이터 목록
	// 각 DA의 SpawnWeightBase 비율에 따라 뽑힘
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TArray<TObjectPtr<UCSCustomerArchetypeDataAsset>> Archetypes;

	// 청결도 : 손님이 오늘 진열대 이동 중 통로에 쓰레기를 버릴 확률
	UPROPERTY(EditAnywhere, Category = "Spawn|Trash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrashDropChance = 0.30f;

	// 청결도 : 버릴 경우, 몇 번째 진열대 이동에서 버릴지의 범위(양끝 포함, 균등 분포)
	UPROPERTY(EditAnywhere, Category = "Spawn|Trash", meta = (ClampMin = "1"))
	int32 MinTrashDropTravelIndex = 1;

	UPROPERTY(EditAnywhere, Category = "Spawn|Trash", meta = (ClampMin = "1"))
	int32 MaxTrashDropTravelIndex = 2;

	// 청결도 : 그 이동에서 진행률(0~1) 중 몇 %쯤에서 버릴지의 범위
	UPROPERTY(EditAnywhere, Category = "Spawn|Trash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinTrashDropProgress = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Spawn|Trash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxTrashDropProgress = 0.8f;

private:
	//스포너가 만든 손님들 중 레벨에 존재하고 있는 배열
	UPROPERTY()
	TArray<TObjectPtr<ACSCustomerCharacter>> SpawnedCustomers;

	// 상점 레벨과 피크 신호를 읽어 오는 게임 상태
	UPROPERTY()
	TObjectPtr<ACSStoreGameState> CachedStoreGameState;

	//손님들의 생성하는 반복 타이머를 가리키는 핸들
	FTimerHandle SpawnTimerHandle;

	// 피크 시작 이벤트와 종료 이벤트 사이에서만 참
	bool bIsPeakActive = false;

	// 그날 영업에 적용 중인 평판 등급
	// 개점 때 한 번 저장하고 그날 마감까지 바뀌지 않는다.
	// 영업 도중 평판이 달라져도 이 값은 그대로이므로 스폰률이 일정
	ECSReputationGrade AppliedReputationGrade = ECSReputationGrade::Normal;

	// 그날 개점 시점에 스캔해둔 매장 내 진열대 개수
	int32 CachedShelfCountAtOpen = 0;

	// 게임 상태에서 현재 평판 등급을 읽어 AppliedReputationGrade에 반영
	// 호출처는 BeginPlay와 개점 전환 둘뿐이다.
	void RefreshAppliedReputationGrade();

	// UCSCustomerRuntimeSubsystem에 등록된 진열대 개수를 읽어 CachedShelfCountAtOpen에 반영
	void RefreshCachedShelfCount();

	// 생성 간격과 동시 인원 상한을 계산할 때 쓰는 평판 등급.
	// 게임 상태를 실시간으로 조회하지 않고 개점에 찍어 둔 평판으로 돌려준다.
	ECSReputationGrade GetReputationGrade() const;

	// 현재 평판 등급과 피크 상태에 해당하는 생성 간격과 동시 인원 상한
	float GetCurrentSpawnInterval() const;
	int32 GetCurrentMaxAlive() const;

	// 현재 생성 간격으로 스폰 타이머를 다시 검
	void ApplySpawnTimer();

	// 종족 배정 + 손님별 파생 시드 발급용 마스터 스트림
	// PickArchetypeBySpawnWeight가 const라서 mutable 필요
	mutable FRandomStream MasterStream;

	// Archetypes 목록에서 SpawnWeightBase 비율에 따라 하나를 확률적으로 고름
	UCSCustomerArchetypeDataAsset* PickArchetypeBySpawnWeight() const;

	// 종족의 Visual 데이터를 읽어 손님 캐릭터의 메시/애니메이션을 적용함
	void ApplyCustomerVisual(ACSCustomerCharacter* Customer, const UCSCustomerVisualDataAsset* Visual) const;

	// 아키타입의 VillainChance / VillainActionWeights를 읽어 이 손님의 빌런 행동을 결정.
	ECSVillainAction RollVillainAction(const UCSCustomerArchetypeDataAsset* Archetype,FRandomStream& Stream) const;

	// 손님이 스폰될 때 가지게 될 캡슐의 크기(반경, 반높이)를 계산
	// 이유: 박스를 캡슐 반경만큼 줄여 손님이 영역 밖으로 삐져나오지 않게 검사하기 위해
	bool GetSpawnBodySize(float CharacterScale, float& OutRadius, float& OutHalfHeight) const;

    // 지정된 영역 내부에서 NavMesh 위의 랜덤 지점을 반환
	// 이유: 크기가 있는 영역를 주면 그 안에서 흩어져서 생성되게 만들며, 빈 액터를 주면 예전처럼 한 점으로 작동하도록 함
	FVector PickPointInArea(const AActor* AreaActor, const FVector& FallbackLocation,
		float BodyRadius, float BodyHalfHeight) const;

	// 특정 위치에 손님이 스폰되었을 때 벽, 기둥, 진열대 등 지오메트리에 끼이는지 검사
	// 영역 스폰을 하다가 특정 장애물에 파묻힌 채로 생성을 방지하기 위해 스폰 전 미리 검사
	bool IsBodyBlockedAt(const FVector& CapsuleCenter, float BodyRadius, float BodyHalfHeight) const;
};
