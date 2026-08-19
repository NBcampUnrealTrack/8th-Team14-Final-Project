#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSCustomerTrafficDataAsset.generated.h"

// 상점 레벨과 피크타임에 따라 손님 생성 간격과 동시 인원을 정하는 데이터 에셋
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSCustomerTrafficDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UCSCustomerTrafficDataAsset();

	// 배열 인덱스 + 1이 상점 레벨이며, 값은 해당 레벨의 손님 생성 간격(초)입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize,
		Category = "Traffic", meta = (ClampMin = "0.1", UIMin = "0.1"))
	TArray<float> LevelSpawnIntervalSeconds;

	// 배열 인덱스 + 1이 상점 레벨이며, 값은 해당 레벨에 동시에 존재할 수 있는 손님 수입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize,
		Category = "Traffic", meta = (ClampMin = "1", UIMin = "1"))
	TArray<int32> LevelMaxAliveCustomers;

	// 피크타임 동안 생성 간격은 이 값으로 나누고, 동시 인원은 이 값으로 곱합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "Traffic", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float PeakMultiplier = 1.8f;

	// TODO(평판): 평판 점수 기준이 확정되면 등급별 배율을 여기에 추가

	// 배열이 비어 있거나 레벨이 범위를 벗어나도 안전한 값을 돌려줍니다.
	UFUNCTION(BlueprintPure, Category = "Traffic")
	float GetSpawnIntervalSeconds(int32 StoreLevel, bool bIsPeakActive) const;

	UFUNCTION(BlueprintPure, Category = "Traffic")
	int32 GetMaxAliveCustomers(int32 StoreLevel, bool bIsPeakActive) const;

	// 해당 레벨의 값이 두 배열에 실제로 들어 있는지 확인합니다.
	// 거짓이면 위 두 함수가 가장 가까운 레벨의 값으로 대체하므로, 호출한 쪽에서 알려 줄 수 있게 열어 둡니다.
	UFUNCTION(BlueprintPure, Category = "Traffic")
	bool HasValuesForLevel(int32 StoreLevel) const;
};
