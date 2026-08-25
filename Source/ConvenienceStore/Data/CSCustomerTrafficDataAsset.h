#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CSProductTypes.h"   // ECSReputationGrade
#include "CSCustomerTrafficDataAsset.generated.h"


USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSReputationTrafficRow
{
	GENERATED_BODY()

	FCSReputationTrafficRow() = default;


	FCSReputationTrafficRow(ECSReputationGrade InGrade,float InSpawnIntervalSeconds,int32 InMaxAliveCustomers): Grade(InGrade), SpawnIntervalSeconds(InSpawnIntervalSeconds), MaxAliveCustomers(InMaxAliveCustomers)
	{
	}

	// 평판 등급의 값
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic")
	ECSReputationGrade Grade = ECSReputationGrade::VeryBad;

	// 몇 초에 한 명씩 만들지 정함 스폰 타이머의 반복 주기가 되는 값입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic",
		meta = (ClampMin = "0.1", UIMin = "0.1"))
	float SpawnIntervalSeconds = 60.0f;

	// 만들어진 손님이 맵에 최대 몇 명까지 남아 있을 수 있는지 정합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traffic",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxAliveCustomers = 3;
};

// 평판 등급과 피크타임에 따라 손님 생성 간격과 맵 최대 인원을 정하는 데이터 에셋
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSCustomerTrafficDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UCSCustomerTrafficDataAsset();

	// 평판 등급별 유입 설정입니다. VeryBad(1) ~ Excellent(5) 다섯 칸으로 설정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "Traffic")
	TArray<FCSReputationTrafficRow> GradeRows;

	// 피크타임 동안 생성 간격은 이 값으로 나누고, 맵 최대 인원은 이 값으로 곱합니다.
	// 간격만 줄이고 상한을 그대로 두면 늘어난 유입이 상한에서 잘려 버리므로 둘 다 건드립니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "Traffic", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float PeakMultiplier = 2.0f;

	// 피크타임까지 반영한 뒤의 맵 최대 인원을 최종적으로 가두는 하드 상한입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "Traffic", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumAliveCustomers = 12;

	// 스폰 타이머의 반복 주기를 정할 때 스포너가 호출
	UFUNCTION(BlueprintPure, Category = "Traffic")
	float GetSpawnIntervalSeconds(ECSReputationGrade Grade, bool bIsPeakActive) const;

	// 손님을 만들기 직전에 스포너가 호출
	UFUNCTION(BlueprintPure, Category = "Traffic")
	int32 GetMaxAliveCustomers(ECSReputationGrade Grade, bool bIsPeakActive) const;

	//데이터 에셋에 값을 실수로 잘못 넣었을 때 이를 감지하고 경고해 주는 '검수 시스템'입니다.
	//스포너가 실행될 때 이 함수를 체크해서 에러 로그를 남깁니다.
	UFUNCTION(BlueprintPure, Category = "Traffic")
	bool ValidateTrafficData(FString& OutReason) const;

private:
	// 현재 평판 등급에 맞는 설정값 찾기
	// 만약 정확한 등급 설정이 없으면, 튕기지 않고 그보다 한 단계 낮은 설정으로 대체
	const FCSReputationTrafficRow* FindRowForGrade(ECSReputationGrade Grade) const;
};
