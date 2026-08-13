#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "GameFramework/Actor.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"

UCSPurchaseDecisionComponent::UCSPurchaseDecisionComponent()
{
	// 매 프레임 계산이 필요한 로직이 아니라 손님이 진열대 앞에 섰을 때만 호출되면 되므로 Tick은 꺼두기
	PrimaryComponentTick.bCanEverTick = false;

	// 스포너가 InitializeRandomStream을 호출하기 전에도 동작은 하도록 기본 시드를 잡아둠
	// bUseFixedSeed가 꺼져 있으면 엔진 전역 RNG에서 시드를 한 번만 얻어옴(이후 굴림은 전부 이 스트림 안에서만 진행)
	RandomStream.Initialize(bUseFixedSeed ? FixedSeed : FMath::Rand());
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

	// 데드존 이내면 편차 자체를 0으로 취급 (손님이 차이를 못 느낌 -> 완전 중립)
	// 데드존을 벗어나면 그만큼만 시그모이드에 반영
	float D = DeviationRatio;
	if (FMath::Abs(D) <= DeadZoneRatio)
	{
		D = 0.f;
	}
	else
	{
		D -= FMath::Sign(D) * DeadZoneRatio;
	}

	// 0.5 중립 시그모이드: 준거가격과 같으면 0.5(중립), 싸질수록 1.0에 가까워지고
	// 비싸질수록 0.0에 가까워짐
	return 1.0f / (1.0f + FMath::Exp(Steepness * D));
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
		return -1.0f;
	}

	// 데드존/가파름 값을 Archetype에서 가져옴
	const float DeadZoneRatio = Archetype ? Archetype->AcceptanceLatitude : FallbackAcceptanceLatitude;
	const float Elasticity = Archetype ? Archetype->PriceElasticity : FallbackPriceElasticity;
	const float Steepness = 3.0f * Elasticity;

	const float ReferencePrice = static_cast<float>(ProductRow->RecommendedSalePrice);
	const UCSStorePricingSubsystem* PricingSubsystem =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<UCSStorePricingSubsystem>()
			: nullptr;
	const float ActualPrice = static_cast<float>(
		IsValid(PricingSubsystem)
			? PricingSubsystem->GetSalePrice(Candidate.ProductRowHandle)
			: ProductRow->RecommendedSalePrice);
	const float PriceScore = ComputePriceScore(ActualPrice, ReferencePrice, DeadZoneRatio, Steepness);

	const float CategoryScore = ComputeCategoryScore(*ProductRow);

	// 하드 차단 신호(-1.0)를 받으면 가격 점수가 아무리 좋아도
	// 이 상품은 애초에 구매 후보가 될 수 없으므로 구매 불가 신호 전파
	if (CategoryScore < 0.f)
	{
		return -1.0f;
	}

	// 예산 점수 계산
	float BudgetScore = 1.0f; // 손님 상태를 못 찾으면 예산 판단 없이 통과시킴 (안전 폴백)
	if (UCSCustomerStateComponent* State = GetCustomerState())
	{
		EnsureBudgetInitialized(*State);

		const float OverrunPenalty = Archetype ? Archetype->BudgetOverrunPenalty : FallbackBudgetOverrunPenalty;
		BudgetScore = ComputeBudgetScore(ActualPrice, State->GetRemainingSoftBudget(), State->SoftBudgetGold, State->GetRemainingHardWallet(), OverrunPenalty);

		// 하드 지갑 초과(-1.0)면 카테고리 차단과 동일하게 구매 불가 신호 전파
		if (BudgetScore < 0.f)
		{
			return -1.0f;
		}
	}

	// 가중합을 가중치 합계로 나눠 정규화
	// 이렇게 해두면 나중에 예산 점수 항(W_Budget 등)이 추가되어도
	// 항상 0~1 범위를 벗어나지 않게 자동으로 맞춰짐
	const float Weighted = (W_Price * PriceScore) + (W_Category * CategoryScore) + (W_Budget * BudgetScore);
	const float WeightSum = W_Price + W_Category + W_Budget;
	const float Normalized = (WeightSum > 0.f) ? (Weighted / WeightSum) : 0.f;

	return FMath::Clamp(Normalized, 0.f, 1.f);
}

// 전달받은 후보 전체에 점수를 매긴 뒤 점수가 높은 순으로 정렬하고
// 상위 몇개(Archetype의 TopCandidateCount)만 남겨서 반환
// 여기서 잘려나간 후보들은 이후 softmax 선택 대상에서 아예 제외됨
TArray<FCSPurchaseCandidate> UCSPurchaseDecisionComponent::ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates) const
{
	// 후보 하나하나에 점수를 매기되, 구매 불가 신호(-1.0)를 받은 후보는
	// 애초에 배열에 넣지 않음 → softmax 대상에서 아예 제외되어 선택 확률이 정확히 0%가 됨
	TArray<FCSPurchaseCandidate> Viable;
	Viable.Reserve(Candidates.Num());

	for (FCSPurchaseCandidate& Candidate : Candidates)
	{
		const float Score = ScoreCandidate(Candidate);
		if (Score < 0.f)
		{
			continue;
		}
		Candidate.Score = Score;
		Viable.Add(Candidate);
	}

	// 점수가 높은 순으로 정렬
	Viable.Sort([](const FCSPurchaseCandidate& A, const FCSPurchaseCandidate& B)
	{
		return A.Score > B.Score;
	});

	// 상위 EffectiveCount개만 남기고 자름
	const int32 EffectiveCount = GetEffectiveTopCandidateCount();
	if (Viable.Num() > EffectiveCount)
	{
		Viable.SetNum(EffectiveCount);
	}

	return Viable;
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
	const float Roll = RandomStream.FRandRange(0.f, WeightSum);

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

	// 진열대에 구매 불가 상품(차단 카테고리/예산 초과)만 있으면 Viable이 비어있을 수 있음
	// 이 방어가 없으면 다음 줄에서 TopCandidates[0] 접근 시 배열 범위 초과로 크래시함
	if (TopCandidates.IsEmpty())
	{
		return false;
	}

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

UCSCustomerStateComponent* UCSPurchaseDecisionComponent::GetCustomerState() const
{
	AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UCSCustomerStateComponent>() : nullptr;
}

float UCSPurchaseDecisionComponent::SampleStandardNormal() const
{
	const float U1 = RandomStream.FRandRange(KINDA_SMALL_NUMBER, 1.0f);
	const float U2 = RandomStream.FRandRange(0.0f, 1.0f);
	return FMath::Sqrt(-2.0f * FMath::Loge(U1)) * FMath::Cos(2.0f * PI * U2);
}

void UCSPurchaseDecisionComponent::EnsureBudgetInitialized(UCSCustomerStateComponent& State) const
{
	// 이미 뽑아둔 예산이 있으면 다시 뽑지 않음
	if (State.bBudgetInitialized)
	{
		return;
	}

	const int32 MedianGold = Archetype ? Archetype->BudgetMedianGold : FallbackBudgetMedianGold;
	const float LogSigma = Archetype ? Archetype->BudgetLogSigma : FallbackBudgetLogSigma;

	// 로그정규분포: ln(예산)이 정규분포를 따르게 만들어서 결과값(예산)은 항상 양수만 나오게
	// 중간값(Median)을 중심으로 SampleStandardNormal()이 위아래로 흔들어줌
	const float LogMedian = FMath::Loge(static_cast<float>(FMath::Max(MedianGold, 1)));
	const float SampledLogBudget = LogMedian + (LogSigma * SampleStandardNormal());
	const int32 SoftBudget = FMath::Max(FMath::RoundToInt(FMath::Exp(SampledLogBudget)), 1);

	// 하드 지갑 한도는 Archetype이 이미 갖고 있는 계산 함수(GetHardWalletLimitGold)로 구함
	// Archetype이 없으면 폴백 배율로 직접 계산
	const int32 HardWallet = Archetype
		? Archetype->GetHardWalletLimitGold(SoftBudget)
		: FMath::RoundToInt(SoftBudget * FallbackHardWalletRatio);

	State.SoftBudgetGold = SoftBudget;
	State.HardWalletLimitGold = HardWallet;
	State.SpentGold = 0;
	State.bBudgetInitialized = true;
}

float UCSPurchaseDecisionComponent::ComputeBudgetScore(float ActualPrice, int32 RemainingSoftBudget, int32 OriginalSoftBudget, int32 RemainingHardWallet, float OverrunPenalty) const
{
	// 하드 지갑조차 넘으면 취향과 무관하게 물리적으로 살 수 없는 상황
	// -1.0은 ComputeCategoryScore와 동일한 규칙의 무조건 구매 불가 신호값
	if (ActualPrice > static_cast<float>(RemainingHardWallet))
	{
		return -1.0f;
	}

	// 소프트 예산 이내면 아직 여유가 있는 상태이므로 만점
	if (ActualPrice <= static_cast<float>(RemainingSoftBudget))
	{
		return 1.0f;
	}

	// 소프트 예산을 넘은 비율만큼 지수적으로 감점
	const float SafeOriginalSoftBudget = FMath::Max(static_cast<float>(OriginalSoftBudget), 1.0f);
	const float OverrunRatio = (ActualPrice - RemainingSoftBudget) / SafeOriginalSoftBudget;
	return FMath::Exp(-OverrunPenalty * OverrunRatio);
}

float UCSPurchaseDecisionComponent::ContinueShoppingProbability(int32 ItemsInBasket) const
{
	// 담은 개수가 0 이하인 비정상 입력 방어 (최소 1로 취급)
	ensureMsgf(ItemsInBasket >= 1,
		TEXT("ContinueShoppingProbability는 1 이상의 값으로 호출해야 합니다. (받은 값: %d) ")
		TEXT("상품을 n번째로 담은 직후 n을 넣어 호출하세요."),
		ItemsInBasket);
	const int32 SafeItemCount = FMath::Max(ItemsInBasket, 1);

	const float TargetBasketSize = Archetype ? Archetype->TargetBasketSize : 2.7f;
	const float SafeTarget = FMath::Max(TargetBasketSize, 1.01f);
	const float BaseProbability = 1.0f - (1.0f / SafeTarget);

	// 예산 여유도
	float BudgetFactor = 1.0f;
	if (UCSCustomerStateComponent* State = GetCustomerState())
	{
		EnsureBudgetInitialized(*State);

		const float SoftBudget = static_cast<float>(FMath::Max(State->SoftBudgetGold, 1));
		const float RemainingSoftBudget = static_cast<float>(State->GetRemainingSoftBudget());
		const float RemainingBudgetRatio = RemainingSoftBudget / SoftBudget;

		BudgetFactor = FMath::SmoothStep(0.0f, 0.3f, RemainingBudgetRatio);
	}

	// 최종 확률 = 기본확률 × 예산여유도
	// 0.95 상한: 아무리 의욕이 넘쳐도 100% 확정은 아니게 함
	return FMath::Clamp(BaseProbability * BudgetFactor, 0.0f, 0.95f);
}

bool UCSPurchaseDecisionComponent::RollContinueShopping(int32 ItemsInBasket) const
{
	const float ContinueProbability = ContinueShoppingProbability(ItemsInBasket);
	return RandomStream.FRand() < ContinueProbability;
}

void UCSPurchaseDecisionComponent::InitializeRandomStream(int32 Seed)
{
	RandomStream.Initialize(Seed);
}
