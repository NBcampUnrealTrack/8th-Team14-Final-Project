#include "Data/CSCustomerTrafficDataAsset.h"


UCSCustomerTrafficDataAsset::UCSCustomerTrafficDataAsset()
{
	LevelSpawnIntervalSeconds = {
		12.0f,
		11.0f,
		9.0f,
		8.5f,
		7.0f,
		6.0f,
		5.0f,
		4.0f,
		3.0f,
		2.0f
	};

	LevelMaxAliveCustomers = {
		2,
		3,
		3,
		4,
		5,
		6,
		7,
		8,
		9,
		10
	};
}

float UCSCustomerTrafficDataAsset::GetSpawnIntervalSeconds(int32 StoreLevel,bool bIsPeakActive) const
{
	// 레벨이 배열 범위를 벗어나면 가장 가까운 레벨의 값을 사용.
	const int32 LevelIndex = FMath::Clamp(StoreLevel, 1, LevelSpawnIntervalSeconds.Num()) - 1;

	// 배열이 비어 있으면 인덱스가 -1이 되어 그대로 읽을 수 없음
	if (!LevelSpawnIntervalSeconds.IsValidIndex(LevelIndex))
	{
		UE_LOG(LogTemp, Error,TEXT("[%s] LevelSpawnIntervalSeconds가 비어 있어 손님 생성 간격을 계산할 수 없습니다."),*GetName());
		return 1.0f;
	}

	const float Interval = FMath::Max(LevelSpawnIntervalSeconds[LevelIndex], 0.1f);

	if (!bIsPeakActive)
	{
		return Interval;
	}

	// 피크타임에는 간격을 나눠 손님이 더 자주 오게 함
	return Interval / FMath::Max(PeakMultiplier, 1.0f);
}

int32 UCSCustomerTrafficDataAsset::GetMaxAliveCustomers(int32 StoreLevel,bool bIsPeakActive) const
{
	// 레벨이 배열 범위를 벗어나면 가장 가까운 레벨의 값을 사용
	const int32 LevelIndex =
		FMath::Clamp(StoreLevel, 1, LevelMaxAliveCustomers.Num()) - 1;

	// 배열이 비어 있으면 인덱스가 -1이 되어 그대로 읽을 수 없음
	if (!LevelMaxAliveCustomers.IsValidIndex(LevelIndex))
	{
		UE_LOG(LogTemp, Error,TEXT("[%s] LevelMaxAliveCustomers가 비어 있어 동시 손님 수 상한을 계산할 수 없습니다."),*GetName());
		return 1;
	}

	const int32 MaxAlive = FMath::Max(LevelMaxAliveCustomers[LevelIndex], 1);

	if (!bIsPeakActive)
	{
		return MaxAlive;
	}

	// 피크타임에는 상한도 함께 올려야 늘어난 유입이 상한에서 잘리지 않습니다.
	return FMath::Max(FMath::RoundToInt(MaxAlive * FMath::Max(PeakMultiplier, 1.0f)),1);
}

bool UCSCustomerTrafficDataAsset::HasValuesForLevel(int32 StoreLevel) const
{
	const int32 LevelIndex = StoreLevel - 1;
	return LevelSpawnIntervalSeconds.IsValidIndex(LevelIndex) && LevelMaxAliveCustomers.IsValidIndex(LevelIndex);
}
