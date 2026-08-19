#include "Data/CSStoreEconomyDataAsset.h"

bool UCSStoreEconomyDataAsset::IsRentPaymentDay(int32 BusinessDay) const
{
	if (FirstRentBusinessDay < 1 ||
		RentPaymentIntervalDays < 1 ||
		BusinessDay < FirstRentBusinessDay)
	{
		return false;
	}

	return (BusinessDay - FirstRentBusinessDay) % RentPaymentIntervalDays == 0;
}

int32 UCSStoreEconomyDataAsset::GetRentGoldForBusinessDay(int32 BusinessDay) const
{
	int32 SelectedStartBusinessDay = 0;
	int32 SelectedRentGold = 0;
	for (const FCSRentTier& RentTier : RentTiers)
	{
		if (RentTier.StartBusinessDay >= 1 &&
			RentTier.RentGold >= 0 &&
			RentTier.StartBusinessDay <= BusinessDay &&
			RentTier.StartBusinessDay > SelectedStartBusinessDay)
		{
			SelectedStartBusinessDay = RentTier.StartBusinessDay;
			SelectedRentGold = RentTier.RentGold;
		}
	}

	return SelectedRentGold;
}

bool UCSStoreEconomyDataAsset::IsConfigurationValid() const
{
	if (FirstRentBusinessDay < 1 ||
		RentPaymentIntervalDays < 1 ||
		RentTiers.IsEmpty())
	{
		return false;
	}

	for (const FCSRentTier& RentTier : RentTiers)
	{
		if (RentTier.StartBusinessDay < 1 || RentTier.RentGold < 0)
		{
			return false;
		}
	}

	return true;
}
