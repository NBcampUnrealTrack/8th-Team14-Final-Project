// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSCustomerSpawner.generated.h"

class ACSCustomerCharacter;

UCLASS()
class CONVENIENCESTORE_API ACSCustomerSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACSCustomerSpawner();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void TrySpawnCustomer();

	// 스폰할 손님 클래스 (BP_CSCustomer 지정)
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TSubclassOf<ACSCustomerCharacter> CustomerClass;

	// 손님을 몇 초 간격으로 만들지?
    UPROPERTY(EditAnywhere, Category = "Spawn")
    float SpawnInterval = 5.0f;

	// 레벨에 존재 할 수 있는 최대 손님 수
    UPROPERTY(EditAnywhere, Category = "Spawn")
    int32 MaxAliveCustomers = 5;

    //스폰될 위치를 알려주는 액터 참조 (할당안되어있으면 스포너 자신의 위치에서 생성)
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TObjectPtr<AActor> EntrancePoint;

    // 퇴장 지점 (비워두면 입장 지점과 동일)
    UPROPERTY(EditAnywhere, Category = "Spawn")
    TObjectPtr<AActor> ExitPoint;

private:
	//스포너가 만든 손님들 중 레벨에 존재하고 있는 배열
	UPROPERTY()
	TArray<TObjectPtr<ACSCustomerCharacter>> SpawnedCustomers;

	//손님들의 생성하는 반복 타이머를 가리키는 핸들
	FTimerHandle SpawnTimerHandle;
};
