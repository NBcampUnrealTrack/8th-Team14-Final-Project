#include "Data/CSStoreProgressionDataAsset.h"

UCSStoreProgressionDataAsset::UCSStoreProgressionDataAsset()
{
	RequiredTotalExperience = {
		0,
		25000,
		75000,
		150000,
		250000,
		375000,
		525000,
		700000,
		900000,
		1125000
	};

	auto AddReputationThreshold = [this](ECSReputationGrade Grade, int32 MinimumScore)
	{
		FCSReputationGradeThreshold& Threshold = ReputationGradeThresholds.AddDefaulted_GetRef();
		Threshold.Grade = Grade;
		Threshold.MinimumReputationScore = MinimumScore;
	};

	AddReputationThreshold(ECSReputationGrade::VeryBad, 0);
	AddReputationThreshold(ECSReputationGrade::Bad, 2500);
	AddReputationThreshold(ECSReputationGrade::Normal, 5000);
	AddReputationThreshold(ECSReputationGrade::Good, 7500);
	AddReputationThreshold(ECSReputationGrade::Excellent, 10000);
}

int32 UCSStoreProgressionDataAsset::GetMaximumStoreExperience() const
{
	int32 MaximumExperience = 0;
	const int32 MaximumLevel = FMath::Min(RequiredTotalExperience.Num(), 10);
	for (int32 LevelIndex = 0; LevelIndex < MaximumLevel; ++LevelIndex)
	{
		MaximumExperience = FMath::Max(
			MaximumExperience,
			RequiredTotalExperience[LevelIndex]);
	}

	return MaximumExperience;
}

int32 UCSStoreProgressionDataAsset::GetStoreLevelForExperience(
	int32 StoreExperience) const
{
	const int32 SafeExperience = FMath::Max(0, StoreExperience);
	int32 StoreLevel = 1;
	const int32 MaximumLevel = FMath::Min(RequiredTotalExperience.Num(), 10);
	for (int32 LevelIndex = 0; LevelIndex < MaximumLevel; ++LevelIndex)
	{
		if (SafeExperience >= FMath::Max(0, RequiredTotalExperience[LevelIndex]))
		{
			StoreLevel = LevelIndex + 1;
		}
	}

	return FMath::Clamp(StoreLevel, 1, 10);
}

ECSReputationGrade UCSStoreProgressionDataAsset::GetReputationGradeForScore(
	int32 ReputationScore) const
{
	const int32 SafeMinimumScore = FMath::Max(0, MinimumReputationScore);
	const int32 SafeMaximumScore = FMath::Max(SafeMinimumScore, MaximumReputationScore);
	const int32 SafeScore = FMath::Clamp(
		ReputationScore,
		SafeMinimumScore,
		SafeMaximumScore);
	ECSReputationGrade Result = ECSReputationGrade::VeryBad;

	for (const FCSReputationGradeThreshold& Threshold : ReputationGradeThresholds)
	{
		if (Threshold.Grade == ECSReputationGrade::None ||
			SafeScore < Threshold.MinimumReputationScore)
		{
			continue;
		}

		if (static_cast<uint8>(Threshold.Grade) > static_cast<uint8>(Result))
		{
			Result = Threshold.Grade;
		}
	}

	return Result;
}
