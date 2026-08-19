// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
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

	// 레벨이 오르면 생성 간격과 동시 인원 상한이 함께 바뀜
	UFUNCTION()
	void HandleStoreLevelChanged(int32 NewLevel);

	// 스폰할 손님 클래스 (BP_CSCustomer 지정)
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TSubclassOf<ACSCustomerCharacter> CustomerClass;

	// 상점 레벨별 생성 간격과 동시 인원, 피크타임 배율을 담은 설정
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

    //스폰될 위치를 알려주는 액터 참조 (할당안되어있으면 스포너 자신의 위치에서 생성)
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TObjectPtr<AActor> EntrancePoint;

    // 퇴장 지점 (비워두면 입장 지점과 동일)
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TObjectPtr<AActor> ExitPoint;

	// 가게 문 앞 판정 지점.
	// 영업 중이면 매장에 들어가고, 아니면 ExitPoint로 계속 걸어갑니다.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TObjectPtr<AActor> StorefrontPoint;


	// 스폰 시 손님에게 확률적으로 배정할 종족 데이터 목록
	// 각 DA의 SpawnWeightBase 비율에 따라 뽑힘
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TArray<TObjectPtr<UCSCustomerArchetypeDataAsset>> Archetypes;

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

	// 게임 상태가 없을 때도 안전하도록 상점 레벨은 이 함수로만 읽음
	int32 GetStoreLevel() const;

	// 현재 레벨과 피크 상태에 해당하는 생성 간격과 동시 인원 상한
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
};
