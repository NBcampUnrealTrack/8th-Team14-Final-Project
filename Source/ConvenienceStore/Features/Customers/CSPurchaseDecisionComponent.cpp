#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSProductTypes.h"

UCSPurchaseDecisionComponent::UCSPurchaseDecisionComponent()
{
	// 매 프레임 계산이 필요한 로직이 아니라 손님이 진열대 앞에 섰을 때만 호출되면 되므로 Tick은 꺼두기
	PrimaryComponentTick.bCanEverTick = false;
}

const FCSProductDataRow* UCSPurchaseDecisionComponent::GetProductRow(const FCSPurchaseCandidate& Candidate) const
{
	return Candidate.ProductRowHandle.GetRow<FCSProductDataRow>(TEXT("CSPurchaseDecisionComponent::GetProductRow"));
}

float UCSPurchaseDecisionComponent::ComputePriceScore(float ActualPrice, float ReferencePrice) const
{
	// 준거가격이 0 이하인 비정상 데이터는 판단 불가 -> 중립값으로 안전 처리
	if (ReferencePrice <= 0.f)
	{
		return 0.5f;
	}

	// 준거가격 대비 얼마나 벗어났는지를 비율로 계산 (부호 있음: + 비쌈 / - 쌈)
	const float DeviationRatio = (ActualPrice - ReferencePrice) / ReferencePrice;

	// 대칭 판단이므로 부호는 버리고 얼마나 벗어났는지 크기만 봄
	const float AbsDeviation = FMath::Abs(DeviationRatio);

	// 데드존 이내: 손님이 차이를 못 느낌 -> 만점
	if (AbsDeviation <= PriceDeadZoneRatio)
	{
		return 1.0f;
	}

	// 데드존을 벗어난 만큼만 시그모이드에 넣음
	const float ExceedAmount = AbsDeviation - PriceDeadZoneRatio;
	return 2.0f / (1.0f + FMath::Exp(PriceSteepness * ExceedAmount));
}

float UCSPurchaseDecisionComponent::ScoreCandidate(const FCSPurchaseCandidate& Candidate) const
{
	const FCSProductDataRow* ProductRow = GetProductRow(Candidate);
	if (!ProductRow)
	{
		// 상품 데이터를 못 찾으면 이 후보는 아예 고려하지 않음
		return 0.f;
	}

	// 1) 가격 효용 점수
	// 지금은 실제 판매가와 손님이 기대하는 준거가격을 따로 설정하지 않아서 필요없지만 추후 수정 예정
	const float ReferencePrice = static_cast<float>(ProductRow->RecommendedSalePrice);
	const float ActualPrice = static_cast<float>(ProductRow->RecommendedSalePrice);
	const float PriceScore = ComputePriceScore(ActualPrice, ReferencePrice);

	const float Weighted = W_Price * PriceScore;
	return FMath::Clamp(Weighted, 0.f, 1.f);
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
