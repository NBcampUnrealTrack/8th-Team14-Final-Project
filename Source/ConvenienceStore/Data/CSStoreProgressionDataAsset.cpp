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

	StoreLevelDisplayNames = {
		NSLOCTEXT("StoreLevel", "Level1", "갓 문을 연 편의점"),
		NSLOCTEXT("StoreLevel", "Level2", "동네의 작은 편의점"),
		NSLOCTEXT("StoreLevel", "Level3", "아늑한 편의점"),
		NSLOCTEXT("StoreLevel", "Level4", "단골이 찾는 편의점"),
		NSLOCTEXT("StoreLevel", "Level5", "활기 넘치는 편의점"),
		NSLOCTEXT("StoreLevel", "Level6", "번창하는 편의점"),
		NSLOCTEXT("StoreLevel", "Level7", "소문난 편의점"),
		NSLOCTEXT("StoreLevel", "Level8", "왕국의 명물 편의점"),
		NSLOCTEXT("StoreLevel", "Level9", "대륙 최고의 편의점"),
		NSLOCTEXT("StoreLevel", "Level10", "전설의 편의점")
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

int32 UCSStoreProgressionDataAsset::GetRequiredTotalExperienceForLevel(
	int32 StoreLevel) const
{
	const int32 LevelIndex = StoreLevel - 1;
	return RequiredTotalExperience.IsValidIndex(LevelIndex)
		? FMath::Max(0, RequiredTotalExperience[LevelIndex])
		: 0;
}

FText UCSStoreProgressionDataAsset::GetStoreLevelDisplayName(int32 StoreLevel) const
{
	const int32 LevelIndex = StoreLevel - 1;
	if (StoreLevelDisplayNames.IsValidIndex(LevelIndex) &&
		!StoreLevelDisplayNames[LevelIndex].IsEmpty())
	{
		return StoreLevelDisplayNames[LevelIndex];
	}

	return FText::Format(
		NSLOCTEXT("StoreLevel", "FallbackLevelName", "상점 레벨 {0}"),
		FText::AsNumber(FMath::Max(1, StoreLevel)));
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
