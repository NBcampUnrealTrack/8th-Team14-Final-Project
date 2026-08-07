#include "Features/Customers/CSPurchaseDecisionComponent.h"

UCSPurchaseDecisionComponent::UCSPurchaseDecisionComponent()
{
	// 매 프레임 계산이 필요한 로직이 아니라 손님이 진열대 앞에 섰을 때만 호출되면 되므로 Tick은 꺼두기
	PrimaryComponentTick.bCanEverTick = false;
}

float UCSPurchaseDecisionComponent::ScoreCandidate(const FCSPurchaseCandidate& Candidate) const
{
	return 0.5f;
}

TArray<FCSPurchaseCandidate> UCSPurchaseDecisionComponent::ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates) const
{
	// 후보 하나하나에 점수 매기기
	for (FCSPurchaseCandidate& Candidate : Candidates)
	{
		Candidate.Score = ScoreCandidate(Candidate);
	}

	// 점수가 높은 순으로 정렬
	Candidates.Sort([](const FCSPurchaseCandidate& A, const FCSPurchaseCandidate& B)
	{
		return A.Score > B.Score;
	});

	// 상위 TopCandidateCount개만 남기고 자르기
	if (Candidates.Num() > TopCandidateCount)
	{
		Candidates.SetNum(TopCandidateCount);
	}

	return Candidates;
}

int32 UCSPurchaseDecisionComponent::SoftmaxSelectIndex(const TArray<FCSPurchaseCandidate>& TopCandidates) const
{
	// 온도가 0 이하로 잘못 설정되는 사고를 막기 위한 안전장치
	const float SafeTemperature = FMath::Max(SelectionTemperature, KINDA_SMALL_NUMBER);

	// 1) 각 후보의 exp(점수 / T)를 계산해서 합 구하기
	TArray<float> Weights;
	Weights.Reserve(TopCandidates.Num());

	float WeightSum = 0.f;
	for (const FCSPurchaseCandidate& Candidate : TopCandidates)
	{
		const float Weight = FMath::Exp(Candidate.Score / SafeTemperature);
		Weights.Add(Weight);
		WeightSum += Weight;
	}

	// 2) 0~WeightSum 사이 난수를 뽑아 누적합이 그 난수를 처음 넘는 지점을 고르기
	const float Roll = FMath::FRandRange(0.f, WeightSum);

	float CumulativeWeight = 0.f;
	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		CumulativeWeight += Weights[Index];
		if (Roll <= CumulativeWeight)
		{
			return Index;
		}
	}

	// 부동소수점 오차로 못 걸렸을 때를 대비한 폴백 (마지막 인덱스 반환)
	return Weights.Num() - 1;
}

bool UCSPurchaseDecisionComponent::DecidePurchase(const TArray<FCSPurchaseCandidate>& Candidates, FCSPurchaseCandidate& OutSelected) const
{
	// 후보가 아예 없으면 미구매
	if (Candidates.IsEmpty())
	{
		return false;
	}

	// 점수 매기고 상위 후보만 추림
	const TArray<FCSPurchaseCandidate> TopCandidates = ScoreAndSelectTopCandidates(Candidates);

	// 1등 점수가 최소 구매 임계치보다 낮으면 손님이 살 게 없다고 판단하고 지나감
	if (TopCandidates[0].Score < MinPurchaseUtility)
	{
		return false;
	}

	// 상위 후보들 중 softmax 확률로 하나 뽑기
	const int32 SelectedIndex = SoftmaxSelectIndex(TopCandidates);
	OutSelected = TopCandidates[SelectedIndex];
	return true;
}
