#include "Data/CSCustomerArchetypeDataAsset.h"

int32 UCSCustomerArchetypeDataAsset::GetHardWalletLimitGold(int32 SoftBudgetGold) const
{
	// 한도 = 소프트 예산 × HardWalletRatio
	//
	// 소프트 예산은 이 정도 쓸 생각이었다!
	// 하드 한도는 지갑에 이만큼밖에 없다!
	// 두 값 사이 구간은 BudgetOverrunPenalty로 서서히 감쇠시킴
	if (SoftBudgetGold <= 0)
	{
		return 0;
	}

	const float HardLimit = static_cast<float>(SoftBudgetGold) * HardWalletRatio;

	// HardWalletRatio를 1.0 미만으로 잘못 넣어도 하드 한도가 소프트 예산보다
	// 작아지지 않게 막음 (그렇게 되면 쓸 생각한 돈조차 못 내는 모순이 생김)
	return FMath::Max(SoftBudgetGold, FMath::RoundToInt(HardLimit));
}
