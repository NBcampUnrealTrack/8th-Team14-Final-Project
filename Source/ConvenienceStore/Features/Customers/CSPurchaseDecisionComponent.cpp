#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSCustomerArchetypeDataAsset.h"

UCSPurchaseDecisionComponent::UCSPurchaseDecisionComponent()
{
	// 매 프레임 계산이 필요한 로직이 아니라 손님이 진열대 앞에 섰을 때만 호출되면 되므로 Tick은 꺼두기
	PrimaryComponentTick.bCanEverTick = false;
}

const FCSProductDataRow* UCSPurchaseDecisionComponent::GetProductRow(const FCSPurchaseCandidate& Candidate) const
{
	return Candidate.ProductRowHandle.GetRow<FCSProductDataRow>(TEXT("CSPurchaseDecisionComponent::GetProductRow"));
}

// Archetype이 있으면 그 값을 없으면 폴백값을 쓰는 헬퍼들
int32 UCSPurchaseDecisionComponent::GetEffectiveTopCandidateCount() const
{
	return Archetype ? Archetype->TopCandidateCount : FallbackTopCandidateCount;
}

float UCSPurchaseDecisionComponent::GetEffectiveSelectionTemperature() const
{
	return Archetype ? Archetype->SelectionTemperature : FallbackSelectionTemperature;
}

float UCSPurchaseDecisionComponent::GetEffectiveMinPurchaseUtility() const
{
	return Archetype ? Archetype->MinPurchaseUtility : FallbackMinPurchaseUtility;
}


float UCSPurchaseDecisionComponent::ComputePriceScore(float ActualPrice, float ReferencePrice, float DeadZoneRatio, float Steepness) const
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
	if (AbsDeviation <= DeadZoneRatio)
	{
		return 1.0f;
	}

	// 데드존을 벗어난 만큼만 시그모이드에 넣음
	const float ExceedAmount = AbsDeviation - DeadZoneRatio;
	return 2.0f / (1.0f + FMath::Exp(Steepness * ExceedAmount));
}

float UCSPurchaseDecisionComponent::ComputeCategoryScore(const FCSProductDataRow& ProductRow) const
{
	if (!Archetype)
	{
		return 0.5f;
	}

	// 하드 차단 카테고리는 선호도 수치와 무관하게 이 상품 자체를 배제함
	// -1.0은 0~1 점수 범위 밖의 무조건 미구매 특수 신호값
	if (Archetype->BlockedCategories.Contains(ProductRow.Category))
	{
		return -1.0f;
	}

	// 선호도 맵에 이 카테고리가 등록되어 있지 않으면
	// 아직 특별히 정해지지 않은 카테고리로 보고 평균(0.5)을 명시적으로 돌려줌
	if (const float* FoundPreference = Archetype->CategoryPreference.Find(ProductRow.Category))
	{
		return *FoundPreference;
	}
	return 0.5f;
}

// 후보 상품 하나에 대해 가격 점수와 카테고리 점수를 각각 구한 뒤
// 가중 평균으로 합쳐 최종 0~1 점수를 만드는 함수
float UCSPurchaseDecisionComponent::ScoreCandidate(const FCSPurchaseCandidate& Candidate) const
{
	const FCSProductDataRow* ProductRow = GetProductRow(Candidate);
	if (!ProductRow)
	{
		// 상품 데이터를 못 찾으면 이 후보는 아예 고려하지 않음
		return 0.f;
	}

	// 데드존/가파름 값을 Archetype에서 가져옴
	const float DeadZoneRatio = Archetype ? Archetype->AcceptanceLatitude : FallbackAcceptanceLatitude;
	const float Elasticity = Archetype ? Archetype->PriceElasticity : FallbackPriceElasticity;
	const float Steepness = 3.0f * Elasticity;

	const float ReferencePrice = static_cast<float>(ProductRow->RecommendedSalePrice);
	const float ActualPrice = static_cast<float>(ProductRow->RecommendedSalePrice); // TODO(3-1): 실제 판매가로 교체
	const float PriceScore = ComputePriceScore(ActualPrice, ReferencePrice, DeadZoneRatio, Steepness);

	const float CategoryScore = ComputeCategoryScore(*ProductRow);

	// 하드 차단 신호(-1.0)를 받으면 가격 점수가 아무리 좋아도
	// 이 상품은 애초에 구매 후보가 될 수 없으므로 즉시 0점으로 반환
	if (CategoryScore < 0.f)
	{
		return 0.f;
	}

	// 가중합을 가중치 합계로 나눠 정규화
	// 이렇게 해두면 나중에 예산 점수 항(W_Budget 등)이 추가되어도
	// 항상 0~1 범위를 벗어나지 않게 자동으로 맞춰짐
	const float Weighted = (W_Price * PriceScore) + (W_Category * CategoryScore);
	const float WeightSum = W_Price + W_Category;
	const float Normalized = (WeightSum > 0.f) ? (Weighted / WeightSum) : 0.f;

	return FMath::Clamp(Normalized, 0.f, 1.f);
}

// 전달받은 후보 전체에 점수를 매긴 뒤 점수가 높은 순으로 정렬하고
// 상위 몇개(Archetype의 TopCandidateCount)만 남겨서 반환
// 여기서 잘려나간 후보들은 이후 softmax 선택 대상에서 아예 제외됨
TArray<FCSPurchaseCandidate> UCSPurchaseDecisionComponent::ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates) const
{
	// 후보 하나하나에 점수를 매김
	for (FCSPurchaseCandidate& Candidate : Candidates)
	{
		Candidate.Score = ScoreCandidate(Candidate);
	}

	// 점수가 높은 순으로 정렬
	Candidates.Sort([](const FCSPurchaseCandidate& A, const FCSPurchaseCandidate& B)
	{
		return A.Score > B.Score;
	});

	// 상위 EffectiveCount개만 남기고 자름
	const int32 EffectiveCount = GetEffectiveTopCandidateCount();
	if (Candidates.Num() > EffectiveCount)
	{
		Candidates.SetNum(EffectiveCount);
	}

	return Candidates;
}

// 상위 후보들 중 점수가 높을수록 뽑힐 확률이 높은
// 그치만 무조건 1등만 뽑히지는 않는 확률적 선택(softmax)을 수행
int32 UCSPurchaseDecisionComponent::SoftmaxSelectIndex(const TArray<FCSPurchaseCandidate>& TopCandidates) const
{
	// 온도가 0 이하로 설정되어 0으로 나누는 사고가 나지 않도록 최솟값 보장
	const float SafeTemperature = FMath::Max(GetEffectiveSelectionTemperature(), KINDA_SMALL_NUMBER);

	// 각 후보의 exp(점수 / 온도)를 구해 가중치 배열을 만들고 총합을 구함
	// 온도가 낮을수록 점수 차이가 그대로 확 벌어져 1등에 가중치가 쏠리고
	// 온도가 높을수록 가중치들이 서로 비슷해져 고르게 뽑힘
	TArray<float> Weights;
	Weights.Reserve(TopCandidates.Num());

	float WeightSum = 0.f;
	for (const FCSPurchaseCandidate& Candidate : TopCandidates)
	{
		const float Weight = FMath::Exp(Candidate.Score / SafeTemperature);
		Weights.Add(Weight);
		WeightSum += Weight;
	}

	// 0~WeightSum 사이 난수를 뽑아 가중치를 누적해가며
	// 그 난수를 처음 넘어서는 지점의 후보를 선택
	// 가중치가 큰 후보일수록 이 구간이 넓어서 뽑힐 확률이 높아지는 방식
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

	// 부동소수점 오차로 어느 구간에도 안 걸렸을 때를 대비한 폴백
	return Weights.Num() - 1;
}


// 후보가 없거나(빈 목록) 1등 점수조차 최소 구매 임계치에 못 미치면
// 구매하지 않고 false를 반환(=손님이 살 게 없어서 빈손으로 지나감)
// 그 외에는 상위 후보들 중 하나를 확률적으로 골라 OutSelected에 채우고 true를 반환
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
	if (TopCandidates[0].Score < GetEffectiveMinPurchaseUtility())
	{
		return false;
	}

	// 상위 후보들 중 softmax 확률로 하나 뽑기
	const int32 SelectedIndex = SoftmaxSelectIndex(TopCandidates);
	OutSelected = TopCandidates[SelectedIndex];
	return true;
}
