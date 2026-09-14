#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "GameFramework/Actor.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 가격 반응("비싸다")이 시작되는 선이자 연관구매 보너스를 허용하는 선
	// 두 선이 어긋나면 "비싸다고 느끼는데 연관구매로 사는" 손님이 생긴다
	constexpr float PriceReactionNeutralPoint = 0.5f;
}

UCSPurchaseDecisionComponent::UCSPurchaseDecisionComponent()
{
	// 매 프레임 계산이 필요한 로직이 아니라 손님이 진열대 앞에 섰을 때만 호출되면 되므로 Tick은 꺼두기
	PrimaryComponentTick.bCanEverTick = false;

	RandomStream.Initialize(FMath::Rand());

	static ConstructorHelpers::FObjectFinder<UDataTable> ProductMasterDataTableFinder(
		TEXT("/Game/ConvenienceStore/Data/DT_ProductMaster.DT_ProductMaster"));
	if (ProductMasterDataTableFinder.Succeeded())
	{
		ProductMasterDataTable = ProductMasterDataTableFinder.Object;
	}
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

float UCSPurchaseDecisionComponent::GetEffectiveCountDecayLambda() const
{
	return Archetype ? Archetype->CountDecayLambda : FallbackCountDecayLambda;
}

float UCSPurchaseDecisionComponent::GetEffectiveRepeatCategoryPenalty() const
{
	return Archetype ? Archetype->RepeatCategoryPenalty : FallbackRepeatCategoryPenalty;
}

// 가격 하드컷 임계값
float UCSPurchaseDecisionComponent::GetEffectivePriceRejectionThreshold() const
{
	return Archetype ? Archetype->PriceRejectionThreshold : FallbackPriceRejectionThreshold;
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
// 구매 불가(-1.0f) 반환 지점마다 OutRejectionInfo에 사유를 채운다
float UCSPurchaseDecisionComponent::ScoreCandidate(const FCSPurchaseCandidate& Candidate, FCSPurchaseRejectionInfo& OutRejectionInfo) const
{
	const FCSProductDataRow* ProductRow = GetProductRow(Candidate);
	if (!ProductRow)
	{
		// 상품 데이터를 못 찾으면 이 후보는 아예 고려하지 않음 (데이터 무결성 문제라 거절 연출 대상이 아님)
		return -1.0f;
	}

	const FCSPriceEvaluation PriceEval = EvaluatePrice(Candidate, *ProductRow);
	const float PriceScore = PriceEval.PriceScore;

	// 가격 하드컷
	if (PriceScore < GetEffectivePriceRejectionThreshold())
	{
		OutRejectionInfo.Reason = ECSPurchaseRejectReason::PriceRejected;
		OutRejectionInfo.ProductId = Candidate.ProductRowHandle.RowName;
		OutRejectionInfo.ActualPrice = PriceEval.ActualPrice;
		OutRejectionInfo.ReferencePrice = PriceEval.ReferencePrice;
		// ComputePriceReactionSeverity가 하드컷 구간을 항상 [0.6, 1.0]에 배정하므로
		// 여기서 나오는 Severity는 언제나 "화남!!" 문턱을 넘는다 - 하드컷 ⟺ 화남!! 불변식
		OutRejectionInfo.Severity = ComputePriceReactionSeverity(PriceScore);
		return -1.0f;
	}

	const float CategoryScore = ComputeCategoryScore(*ProductRow);

	// 하드 차단 신호(-1.0)를 받으면 가격 점수가 아무리 좋아도
	// 이 상품은 애초에 구매 후보가 될 수 없으므로 구매 불가 신호 전파
	if (CategoryScore < 0.f)
	{
		OutRejectionInfo.Reason = ECSPurchaseRejectReason::CategoryBlocked;
		OutRejectionInfo.ProductId = Candidate.ProductRowHandle.RowName;
		OutRejectionInfo.BlockedCategory = ProductRow->Category;
		// CategoryBlocked는 대표 사유 비교에 안 쓰이는 고정값 - 완전히 관심 없는 것과
		// 아슬아슬하게 놓친 것을 같은 척도로 비교하지 않기 위함
		OutRejectionInfo.Severity = 1.0f;
		return -1.0f;
	}

	// 한계효용 체감: 이미 장바구니에 같은 카테고리 상품이 담겨 있으면
	// 반복 횟수만큼 카테고리 점수를 지수적으로 깎음 (예: 음료를 3개째 보면 선호도가 확 낮아짐)
	// -1.0 특수신호가 아니라 0~1 점수를 줄이는 방식이라 위의 구매 불가 판정과는 별개
	float AdjustedCategoryScore = CategoryScore;
	if (const UCSCustomerStateComponent* State = GetCustomerState())
	{
		int32 RepeatCount = 0;
		for (const FCSSelectProduct& Selected : State->SelectProducts)
		{
			const FCSProductDataRow* SelectedRow = Selected.ProductRow.GetRow<FCSProductDataRow>(
				TEXT("UCSPurchaseDecisionComponent::ScoreCandidate(RepeatCategoryPenalty)"));
			if (SelectedRow != nullptr && SelectedRow->Category == ProductRow->Category)
			{
				RepeatCount++;
			}
		}

		if (RepeatCount > 0)
		{
			AdjustedCategoryScore *= FMath::Pow(GetEffectiveRepeatCategoryPenalty(), static_cast<float>(RepeatCount));
		}
	}

	// 예산 점수 계산
	float BudgetScore = 1.0f; // 손님 상태를 못 찾으면 예산 판단 없이 통과시킴 (안전 폴백)
	if (UCSCustomerStateComponent* State = GetCustomerState())
	{
		EnsureBudgetInitialized(*State);

		const float OverrunPenalty = Archetype ? Archetype->BudgetOverrunPenalty : FallbackBudgetOverrunPenalty;
		BudgetScore = ComputeBudgetScore(PriceEval.ActualPrice, State->GetRemainingSoftBudget(), State->SoftBudgetGold, State->GetRemainingHardWallet(), OverrunPenalty);

		// 하드 지갑 초과(-1.0)면 카테고리 차단과 동일하게 구매 불가 신호 전파
		if (BudgetScore < 0.f)
		{
			OutRejectionInfo.Reason = ECSPurchaseRejectReason::HardWalletExceeded;
			OutRejectionInfo.ProductId = Candidate.ProductRowHandle.RowName;
			OutRejectionInfo.ActualPrice = PriceEval.ActualPrice;
			OutRejectionInfo.ReferencePrice = PriceEval.ReferencePrice;
			const float SafeRemainingHardWallet = FMath::Max(static_cast<float>(State->GetRemainingHardWallet()), 1.0f);
			OutRejectionInfo.Severity = FMath::Clamp((PriceEval.ActualPrice - State->GetRemainingHardWallet()) / SafeRemainingHardWallet, 0.0f, 1.0f);
			return -1.0f;
		}
	}

	// 가중합을 가중치 합계로 나눠 정규화
	// 이렇게 해두면 나중에 예산 점수 항(W_Budget 등)이 추가되어도
	// 항상 0~1 범위를 벗어나지 않게 자동으로 맞춰짐
	const float Weighted = (W_Price * PriceScore) + (W_Category * AdjustedCategoryScore) + (W_Budget * BudgetScore);
	const float WeightSum = W_Price + W_Category + W_Budget;
	const float Normalized = (WeightSum > 0.f) ? (Weighted / WeightSum) : 0.f;

	// 연관구매 보너스는 여기(모든 -1.0 반환을 통과한 뒤, 기존 Clamp 안)에서만 더한다
	// 이보다 앞에 두면 구매 불가 판정을 받았어야 할 상품이 보너스로 되살아날 수 있다
	const float AffinityBonus = ComputeAffinityBonus(ProductRow->Category, PriceScore);

	return FMath::Clamp(Normalized + AffinityBonus, 0.f, 1.f);
}

// 전달받은 후보 전체에 점수를 매긴 뒤 점수가 높은 순으로 정렬하고
// 상위 몇개(Archetype의 TopCandidateCount)만 남겨서 반환
// 여기서 잘려나간 후보들은 이후 softmax 선택 대상에서 아예 제외됨
// 살아남은 후보가 하나도 없으면 OutBestRejectionInfo에 대표 거절 사유를 채운다
TArray<FCSPurchaseCandidate> UCSPurchaseDecisionComponent::ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates, FCSPurchaseRejectionInfo& OutBestRejectionInfo) const
{
	// 후보 하나하나에 점수를 매기되, 구매 불가 신호(-1.0)를 받은 후보는
	// 애초에 배열에 넣지 않음 → softmax 대상에서 아예 제외되어 선택 확률이 정확히 0%가 됨
	TArray<FCSPurchaseCandidate> Viable;
	Viable.Reserve(Candidates.Num());

	// 대표 거절 사유 집계용. 카테고리 차단이 하나라도 있으면 그게 최우선(bFoundBlocked로 확정 후 더 안 봄)
	// 없으면 Severity가 가장 낮은(=가장 아깝게 놓친) PriceRejected/HardWalletExceeded를 대표로 삼음
	bool bFoundBlocked = false;
	bool bFoundAnyRejection = false;

	for (FCSPurchaseCandidate& Candidate : Candidates)
	{
		FCSPurchaseRejectionInfo CandidateRejection;
		const float Score = ScoreCandidate(Candidate, CandidateRejection);
		if (Score < 0.f)
		{
			// 이미 카테고리 차단 사유로 확정됐거나, 상품 데이터를 못 찾아 사유가 안 채워진(Reason=None)
			// 데이터 무결성 문제 후보는 대표 사유 집계에서 제외 - Severity 기본값(0.0f)이
			// 항상 최솟값이라 아무 사유도 없는 후보가 대표로 뽑히는 걸 막기 위함
			if (bFoundBlocked || CandidateRejection.Reason == ECSPurchaseRejectReason::None)
			{
				continue;
			}

			if (CandidateRejection.Reason == ECSPurchaseRejectReason::CategoryBlocked)
			{
				OutBestRejectionInfo = CandidateRejection;
				bFoundBlocked = true;
			}
			else if (!bFoundAnyRejection || CandidateRejection.Severity < OutBestRejectionInfo.Severity)
			{
				OutBestRejectionInfo = CandidateRejection;
			}
			bFoundAnyRejection = true;
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


// 1등 후보는 있지만 총점이 MinPurchaseUtility에 못 미쳐 거절되는 케이스 전용
// ScoreCandidate는 이 경우 정상 점수(0~1)를 반환해 OutRejectionInfo를 안 채우므로 여기서 별도로 채운다
//
// 총점 미달의 원인이 항상 가격은 아님
// 진짜 원인 판정 기준: 반복구매 감쇠를 걷어내도(RepeatCount=0 가정) 여전히 기준선을 못 넘었을까?
// 못 넘었으면(=애초부터 안 살 상품이었음) 진짜 가격/선호 문제라 PriceRejected로 표시
// 넘었으면(=반복구매가 이번에만 결정타) 플레이어에게 알릴 이유가 없는 조용한 미구매라고 판단해 Reason=None으로 남겨둔다(연출 안 함)
FCSPurchaseRejectionInfo UCSPurchaseDecisionComponent::BuildScoreShortfallRejectionInfo(const FCSPurchaseCandidate& Candidate) const
{
	FCSPurchaseRejectionInfo Info;

	const FCSProductDataRow* ProductRow = GetProductRow(Candidate);
	if (ProductRow == nullptr)
	{
		return Info; // 데이터 무결성 문제 - 표시 대상 아님(Reason=None 기본값 그대로)
	}

	const FCSPriceEvaluation PriceEval = EvaluatePrice(Candidate, *ProductRow);

	// 반복구매 감쇠를 걷어낸(RepeatCount=0 가정) 원래 카테고리 점수로 가상 총점을 다시 계산
	// CategoryScore/BudgetScore가 여기서 -1일 수 없음 - 그랬다면 ScoreCandidate가 그 단계에서
	// 이미 -1.0f를 반환해 이 함수(정상 0~1 점수 케이스 전용)까지 오지도 못했을 것이기 때문
	const float RawCategoryScore = ComputeCategoryScore(*ProductRow);

	float BudgetScore = 1.0f;
	if (UCSCustomerStateComponent* State = GetCustomerState())
	{
		const float OverrunPenalty = Archetype ? Archetype->BudgetOverrunPenalty : FallbackBudgetOverrunPenalty;
		BudgetScore = ComputeBudgetScore(PriceEval.ActualPrice, State->GetRemainingSoftBudget(), State->SoftBudgetGold, State->GetRemainingHardWallet(), OverrunPenalty);
	}

	const float WeightSum = W_Price + W_Category + W_Budget;
	const float NormalizedWithoutRepeatPenalty = (WeightSum > 0.f)
		? (W_Price * PriceEval.PriceScore + W_Category * RawCategoryScore + W_Budget * BudgetScore) / WeightSum
		: 0.f;

	// ScoreCandidate와 같은 헬퍼로 같은 보너스를 더한다
	const float AffinityBonus = ComputeAffinityBonus(ProductRow->Category, PriceEval.PriceScore);
	const float ScoreWithoutRepeatPenalty = FMath::Clamp(NormalizedWithoutRepeatPenalty + AffinityBonus, 0.f, 1.f);

	if (ScoreWithoutRepeatPenalty < GetEffectiveMinPurchaseUtility())
	{
		// 반복구매 감쇠를 빼고 봐도 기준선을 못 넘음 - 애초부터 가격/선호가 문제였던 진짜 거절
		Info.Reason = ECSPurchaseRejectReason::PriceRejected;
		Info.ProductId = Candidate.ProductRowHandle.RowName;
		Info.ActualPrice = PriceEval.ActualPrice;
		Info.ReferencePrice = PriceEval.ReferencePrice;
		// 총점 기반이 아니라 다른 두 경로(하드컷/구매성공 시 가격반응)와 동일하게 PriceScore 기반으로
		// 통일 - 세 경로의 Severity가 같은 척도 위에 있어야 DT 티어 문턱이 일관되게 작동함
		Info.Severity = ComputePriceReactionSeverity(PriceEval.PriceScore);
	}
	// else: 반복구매 감쇠가 이번 거절의 진짜 원인 - Reason=None(기본값)으로 두고 조용히 미구매

	return Info;
}

// 후보가 없거나(빈 목록) 1등 점수조차 최소 구매 임계치에 못 미치면
// 구매하지 않고 false를 반환(=손님이 살 게 없어서 빈손으로 지나감)
// 그 외에는 상위 후보들 중 하나를 확률적으로 골라 OutSelected에 채우고 true를 반환
// false를 반환하는 모든 경로에서 OutRejectionInfo에 대표 거절 사유를 채운다
bool UCSPurchaseDecisionComponent::DecidePurchase(const TArray<FCSPurchaseCandidate>& Candidates, FCSPurchaseCandidate& OutSelected, FCSPurchaseRejectionInfo& OutRejectionInfo) const
{

	// 후보가 아예 없으면 미구매 - 진열대 자체가 빈 상태
	if (Candidates.IsEmpty())
	{
		OutRejectionInfo.Reason = ECSPurchaseRejectReason::ShelfEmpty;
		return false;
	}

	// 점수 매기고 상위 후보만 추림 (구매 불가 후보들의 대표 거절 사유는 OutRejectionInfo에 누적됨)
	const TArray<FCSPurchaseCandidate> TopCandidates = ScoreAndSelectTopCandidates(Candidates, OutRejectionInfo);

	// 진열대에 구매 불가 상품(차단 카테고리/예산 초과)만 있으면 Viable이 비어있을 수 있음
	// 이 방어가 없으면 다음 줄에서 TopCandidates[0] 접근 시 배열 범위 초과로 크래시함
	// OutRejectionInfo는 위 ScoreAndSelectTopCandidates 호출에서 이미 채워져 있음
	if (TopCandidates.IsEmpty())
	{
		return false;
	}

	// 1등 점수가 최소 구매 임계치보다 낮으면 손님이 살 게 없다고 판단하고 지나감
	if (TopCandidates[0].Score < GetEffectiveMinPurchaseUtility())
	{
		OutRejectionInfo = BuildScoreShortfallRejectionInfo(TopCandidates[0]);
		return false;
	}

	// 상위 후보들 중 softmax 확률로 하나 뽑기
	const int32 SelectedIndex = SoftmaxSelectIndex(TopCandidates);
	OutSelected = TopCandidates[SelectedIndex];

	// 구매에 성공해도 가격이 어느 정도 부담스러웠으면 반응을 남김
	OutRejectionInfo = BuildPriceReactionInfo(OutSelected);

	// 연관구매 보너스가 있었을 때만 로그
	if (const FCSProductDataRow* SelectedRow = GetProductRow(OutSelected))
	{
		const FCSPriceEvaluation SelectedPriceEval = EvaluatePrice(OutSelected, *SelectedRow);
		FString AnchorSummary;
		const float AffinityBonus = ComputeAffinityBonus(SelectedRow->Category, SelectedPriceEval.PriceScore, &AnchorSummary);
		if (AffinityBonus > 0.f)
		{
			const float FinalScore = OutSelected.Score;
			const float OriginalScore = FinalScore - AffinityBonus;
			// 최종 점수가 상한(1.0)에 잘렸으면 원래 점수를 정확히 역산할 수 없다는 표시
			const bool bClampedAtCap = FinalScore >= 1.0f - KINDA_SMALL_NUMBER;
			const AActor* Owner = GetOwner();
			UE_LOG(LogTemp, Log,
				TEXT("[연관구매] 담기 손님=%s 상품=%s 가격점수=%.2f 원래=%.2f +보너스=%.2f → 최종=%.2f%s 앵커=[%s]"),
				Owner != nullptr ? *Owner->GetName() : TEXT("?"),
				*OutSelected.ProductRowHandle.RowName.ToString(),
				SelectedPriceEval.PriceScore, OriginalScore, AffinityBonus, FinalScore,
				bClampedAtCap ? TEXT("(상한)") : TEXT(""), *AnchorSummary);
		}
	}

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

	// CountDecay: 담을수록 하나 더 담을 확률이 지수적으로 줄어드는 항
	const float CountDecayLambda = GetEffectiveCountDecayLambda();
	const float CountDecay = FMath::Exp(-CountDecayLambda * static_cast<float>(SafeItemCount - 1));

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

	// 최종 확률 = 기본확률 × CountDecay × 예산여유도
	// 0.95 상한: 아무리 의욕이 넘쳐도 100% 확정은 아니게 함
	return FMath::Clamp(BaseProbability * CountDecay * BudgetFactor, 0.0f, 0.95f);
}

bool UCSPurchaseDecisionComponent::RollContinueShopping(int32 ItemsInBasket) const
{
	const float ContinueProbability = ContinueShoppingProbability(ItemsInBasket);
	return RandomStream.FRand() < ContinueProbability;
}

// 스폰 시점에 이번 방문의 목표 장바구니 개수를 미리 뽑아둠
// ContinueShoppingProbability(세션 내부 계속-구매 판단과 동일한 함수)를 재사용하므로
// 두 계산이 항상 같은 확률 곡선 위에서 움직임이 보장됨
int32 UCSPurchaseDecisionComponent::RollPlannedBasketTarget(FRandomStream& Stream) const
{
	int32 Count = 1;
	while (Stream.FRand() < ContinueShoppingProbability(Count))
	{
		Count++;
	}
	return Count;
}

namespace
{
	// RollPlannedProducts 전용 - ECSProductCategory 전체 값을 순회하기 위한 목록
	// (UENUM 리플렉션으로 순회하지 않고 배열로 나열)
	// 카테고리를 추가/삭제하면 이 배열도 반드시 같이 고칠 것
	const ECSProductCategory AllProductCategories[] = {
		ECSProductCategory::Alcohol, ECSProductCategory::Beverage, ECSProductCategory::FreshFood,
		ECSProductCategory::FrozenFood, ECSProductCategory::Potion, ECSProductCategory::Scroll,
		ECSProductCategory::Snack, ECSProductCategory::Supply, ECSProductCategory::Toiletry,
		ECSProductCategory::Tool, ECSProductCategory::HangoverCure
	};

	// Category에 속하면서 CurrentStoreLevel에서 실제로 살 수 있는(StoreLevelUnlockCondition 통과) 상품만 모아
	// RowName과 BaseDemand 가중치를 나란히 채움
	// 이 필터가 없으면 저레벨 매장에서 Beverage/Toiletry/Tool처럼 후보가 아예 0개인 카테고리를
	// 다음 단계(상품 가중 샘플링)에서 빈 배열로 넘기게 됨
	void CollectUnlockedProductsInCategory(
		const UDataTable& ProductTable,
		ECSProductCategory Category,
		int32 CurrentStoreLevel,
		TArray<FName>& OutRowNames,
		TArray<float>& OutWeights)
	{
		for (const TPair<FName, uint8*>& Pair : ProductTable.GetRowMap())
		{
			const FCSProductDataRow* Row = reinterpret_cast<const FCSProductDataRow*>(Pair.Value);
			if (Row == nullptr || Row->Category != Category || Row->StoreLevelUnlockCondition > CurrentStoreLevel)
			{
				continue;
			}
			OutRowNames.Add(Pair.Key);
			OutWeights.Add(static_cast<float>(Row->BaseDemand));
		}

		// BaseDemand가 전부 0(데이터 미기입)이면 가중 샘플링이 전부 0표를 뽑는 꼴이라 균등 가중치로 대체
		float TotalWeight = 0.0f;
		for (const float Weight : OutWeights)
		{
			TotalWeight += Weight;
		}
		if (TotalWeight <= 0.0f)
		{
			for (float& Weight : OutWeights)
			{
				Weight = 1.0f;
			}
		}
	}

	// 누적 가중치 방식으로 인덱스 하나를 뽑음. Weights가 비어있지 않다는 건 호출부가 보장함
	int32 WeightedRandomIndex(const TArray<float>& Weights, FRandomStream& Stream)
	{
		float TotalWeight = 0.0f;
		for (const float Weight : Weights)
		{
			TotalWeight += FMath::Max(Weight, 0.0f);
		}
		if (TotalWeight <= 0.0f)
		{
			return 0;
		}

		const float Roll = Stream.FRandRange(0.0f, TotalWeight);
		float Cumulative = 0.0f;
		for (int32 i = 0; i < Weights.Num(); i++)
		{
			Cumulative += FMath::Max(Weights[i], 0.0f);
			if (Roll <= Cumulative)
			{
				return i;
			}
		}
		return Weights.Num() - 1;
	}
}

// 스폰 시점에 이번 방문에서 우선적으로 찾아다닐 계획 상품(1~2개)을 미리 뽑아둠
// PlannedBasketStream을 그대로 이어받아 쓰므로 여기서 몇 번을 더 굴리든 MasterStream 시퀀스에는 영향 없음
TArray<FName> UCSPurchaseDecisionComponent::RollPlannedProducts(FRandomStream& Stream, int32 CurrentStoreLevel) const
{
	TArray<FName> Result;

	// 계획 유무부터 갈라놓는 굴림 - DT 상태와 무관하게 항상 이 자리에서 1회만 일어나야
	// 입장 판정 쪽에서 계획 있음/없음 어느 쪽이든 굴림 횟수가 같다는 전제가 성립한다
	// 반드시 인수로 받은 스트림(스포너의 파생 스트림)만 쓴다 - 멤버 RandomStream을 쓰면 손님 마스터 시퀀스가 밀린다
	if (Stream.FRand() >= PlannedPurchaseChance)
	{
		return Result;
	}

	if (!IsValid(ProductMasterDataTable))
	{
		return Result;
	}

	// 개수(1~2)부터 이 스트림에서 정함
	const int32 TargetCount = (Stream.FRand() < PlannedProductSecondItemChance) ? 2 : 1;

	for (int32 i = 0; i < TargetCount; i++)
	{
		// 1) 후보 카테고리 수집: BlockedCategories 제외 + 이 레벨에서 살 수 있는 상품이
		// 하나도 없는 카테고리 제외(레벨 필터 - CSV 확인상 Beverage/Toiletry/Tool이 특히 걸림)
		TArray<ECSProductCategory> CandidateCategories;
		TArray<float> CategoryWeights;

		for (const ECSProductCategory Category : AllProductCategories)
		{
			if (Archetype && Archetype->BlockedCategories.Contains(Category))
			{
				continue;
			}

			TArray<FName> ProductsInCategory;
			TArray<float> ProductWeights;
			CollectUnlockedProductsInCategory(*ProductMasterDataTable, Category, CurrentStoreLevel, ProductsInCategory, ProductWeights);
			if (ProductsInCategory.Num() == 0)
			{
				continue;
			}

			// 선호도 맵에 없는 카테고리는 평균(0.5)으로 취급
			const float* FoundPreference = Archetype ? Archetype->CategoryPreference.Find(Category) : nullptr;
			const float RawPreference = FoundPreference ? *FoundPreference : 0.5f;

			// GetCategoryAttractionWeights와 같은 지수를 적용
			const float Weight = FMath::Pow(FMath::Max(RawPreference, 0.0f), CategoryAttractionExponent);

			CandidateCategories.Add(Category);
			CategoryWeights.Add(Weight);
		}

		if (CandidateCategories.Num() == 0)
		{
			// 이 손님의 레벨/차단 카테고리 조건으로는 계획을 세울 상품이 하나도 없음 - 조용히 포기
			break;
		}

		// 2) 카테고리 하나를 선호도 가중 샘플링으로 확정
		const ECSProductCategory PickedCategory = CandidateCategories[WeightedRandomIndex(CategoryWeights, Stream)];

		// 3) 그 카테고리 안에서 BaseDemand 가중 샘플링으로 상품 하나 확정
		TArray<FName> ProductsInPickedCategory;
		TArray<float> ProductWeightsInPickedCategory;
		CollectUnlockedProductsInCategory(*ProductMasterDataTable, PickedCategory, CurrentStoreLevel, ProductsInPickedCategory, ProductWeightsInPickedCategory);
		if (ProductsInPickedCategory.Num() == 0)
		{
			// 위 필터를 이미 통과한 카테고리라 정상적으로는 도달하지 않음 - 방어적 재확인
			continue;
		}

		const FName PickedProduct = ProductsInPickedCategory[WeightedRandomIndex(ProductWeightsInPickedCategory, Stream)];
		Result.Add(PickedProduct);
	}

	return Result;
}

// 카테고리 선호를 손님 동선에 반영하기 위한 카테고리별 끌림 가중치
TMap<ECSProductCategory, float> UCSPurchaseDecisionComponent::GetCategoryAttractionWeights() const
{
	TMap<ECSProductCategory, float> Weights;
	Weights.Reserve(UE_ARRAY_COUNT(AllProductCategories));
	for (const ECSProductCategory Category : AllProductCategories)
	{
		if (Archetype && Archetype->BlockedCategories.Contains(Category))
		{
			Weights.Add(Category, 0.0f);
			continue;
		}

		// 선호도 맵에 없는 카테고리는 평균(0.5)으로 취급
		const float* FoundPreference = Archetype ? Archetype->CategoryPreference.Find(Category) : nullptr;
		const float Preference = FoundPreference ? *FoundPreference : 0.5f;

		// 지수를 씌워 선호 차이를 확률 차이로 뚜렷하게 벌림
		Weights.Add(Category, FMath::Pow(FMath::Max(Preference, 0.0f), CategoryAttractionExponent));
	}

	return Weights;
}

void UCSPurchaseDecisionComponent::InitializeRandomStream(int32 Seed)
{
	RandomStream.Initialize(Seed);
}

bool UCSPurchaseDecisionComponent::RollChance(float Chance) const
{
	return RandomStream.FRand() < FMath::Clamp(Chance, 0.0f, 1.0f);
}

// =====================================================================
// 거절 연출 (인지 채널)
// =====================================================================

// Info.Reason을 DT 행 이름(FName)으로 수동 매핑해 RejectionDisplayDataTable에서 행을 찾고
// Tiers를 Severity 내림차순으로 순회해 Severity >= Threshold인 첫 구간을 반환한다
FCSPurchaseRejectionTier UCSPurchaseDecisionComponent::GetRejectionDisplayTier(const FCSPurchaseRejectionInfo& Info) const
{
	if (!IsValid(RejectionDisplayDataTable))
	{
		return FCSPurchaseRejectionTier();
	}

	FName RowName;
	switch (Info.Reason)
	{
	case ECSPurchaseRejectReason::PriceRejected:
		RowName = TEXT("PriceRejected");
		break;
	case ECSPurchaseRejectReason::CategoryBlocked:
		RowName = TEXT("CategoryBlocked");
		break;
	case ECSPurchaseRejectReason::HardWalletExceeded:
		RowName = TEXT("HardWalletExceeded");
		break;
	case ECSPurchaseRejectReason::ShelfEmpty:
		RowName = TEXT("ShelfEmpty");
		break;
	case ECSPurchaseRejectReason::PlannedProductMissing:
		RowName = TEXT("PlannedProductMissing");
		break;
	default:
		return FCSPurchaseRejectionTier();
	}

	const FCSPurchaseRejectionDisplayRow* Row =
		RejectionDisplayDataTable->FindRow<FCSPurchaseRejectionDisplayRow>(RowName, TEXT("UCSPurchaseDecisionComponent::GetRejectionDisplayTier"));
	if (Row == nullptr || Row->Tiers.IsEmpty())
	{
		return FCSPurchaseRejectionTier();
	}

	for (const FCSPurchaseRejectionTier& Tier : Row->Tiers)
	{
		if (Info.Severity >= Tier.SeverityThreshold)
		{
			return Tier;
		}
	}

	// 어느 구간에도 안 걸렸으면(Severity가 모든 Threshold보다 낮으면) 마지막 구간을 그대로 씀
	return Row->Tiers.Last();
}

// Info.ProductId(RowName)로 ProductMasterDataTable을 조회해 화면 표시용 상품명을 반환
// 못 찾으면(ShelfEmpty처럼 ProductId가 애초에 없는 경우 포함) 빈 텍스트를 반환해
// {ProductName}을 참조하지 않는 기존 템플릿(가격/카테고리 반응 등)에는 영향이 없게 함
FText UCSPurchaseDecisionComponent::GetProductDisplayName(FName ProductId) const
{
	if (ProductId.IsNone() || !IsValid(ProductMasterDataTable))
	{
		return FText::GetEmpty();
	}

	const FCSProductDataRow* Row = ProductMasterDataTable->FindRow<FCSProductDataRow>(
		ProductId, TEXT("UCSPurchaseDecisionComponent::GetProductDisplayName"));
	return Row != nullptr ? Row->DisplayName : FText::GetEmpty();
}

// 위 Tier의 InnerThoughtTemplate에 {ActualPrice}/{ReferencePrice}/{ProductName} 자리표시자를 실제 값으로 채운다
FText UCSPurchaseDecisionComponent::BuildInnerThoughtText(const FCSPurchaseRejectionInfo& Info) const
{
	const FCSPurchaseRejectionTier Tier = GetRejectionDisplayTier(Info);

	FFormatNamedArguments Args;
	Args.Add(TEXT("ActualPrice"), FText::AsNumber(FMath::RoundToInt(Info.ActualPrice)));
	Args.Add(TEXT("ReferencePrice"), FText::AsNumber(FMath::RoundToInt(Info.ReferencePrice)));
	Args.Add(TEXT("ProductName"), GetProductDisplayName(Info.ProductId));
	return FText::Format(Tier.InnerThoughtTemplate, Args);
}

// 실제가/기준가 조회 + 데드존/기울기 반영 PriceScore 계산까지 한 번에 처리하는 공용 헬퍼
// ScoreCandidate/BuildScoreShortfallRejectionInfo/BuildPriceReactionInfo가 전부 이 절차를 반복해서 뽑아냄
UCSPurchaseDecisionComponent::FCSPriceEvaluation UCSPurchaseDecisionComponent::EvaluatePrice(const FCSPurchaseCandidate& Candidate, const FCSProductDataRow& ProductRow) const
{
	FCSPriceEvaluation Eval;
	Eval.ReferencePrice = static_cast<float>(ProductRow.RecommendedSalePrice);

	const UCSStorePricingSubsystem* PricingSubsystem =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<UCSStorePricingSubsystem>()
			: nullptr;
	Eval.ActualPrice = static_cast<float>(
		IsValid(PricingSubsystem)
			? PricingSubsystem->GetSalePrice(Candidate.ProductRowHandle)
			: ProductRow.RecommendedSalePrice);

	const float DeadZoneRatio = Archetype ? Archetype->AcceptanceLatitude : FallbackAcceptanceLatitude;
	const float Elasticity = Archetype ? Archetype->PriceElasticity : FallbackPriceElasticity;
	const float Steepness = 3.0f * Elasticity * PriceSteepnessMultiplier;
	Eval.PriceScore = ComputePriceScore(Eval.ActualPrice, Eval.ReferencePrice, DeadZoneRatio, Steepness);
	return Eval;
}

// PriceScore로 가격에 대한 반응 강도를 0(무반응)~1(최고 강도)로 환산
float UCSPurchaseDecisionComponent::ComputePriceReactionSeverity(float PriceScore) const
{
	if (PriceScore >= PriceReactionNeutralPoint)
	{
		return 0.0f; // 데드존 이내이거나 더 저렴함 - 반응 없음
	}

	const float Threshold = GetEffectivePriceRejectionThreshold();
	if (PriceScore <= Threshold)
	{
		// 하드컷 구간 - 항상 [0.6, 1.0]에 배정 너무 비싸서 화나는 정도의 문턱(DT SeverityThreshold 0.6)을 반드시 넘게 함
		const float SafeThreshold = FMath::Max(Threshold, KINDA_SMALL_NUMBER);
		return 0.6f + 0.4f * FMath::Clamp((Threshold - PriceScore) / SafeThreshold, 0.0f, 1.0f);
	}

	// 데드존 밖 ~ 하드컷 문턱 사이 - [0, 0.6) 구간에 배정
	const float SafeSpan = FMath::Max(PriceReactionNeutralPoint - Threshold, KINDA_SMALL_NUMBER);
	return 0.6f * FMath::Clamp((PriceReactionNeutralPoint - PriceScore) / SafeSpan, 0.0f, 1.0f);
}

// 카테고리 연관구매 점수 보너스
// PriceScore가 중립점보다 낮으면(비싸다고 느끼는 중) 보너스를 주지 않는다 - 안 그러면
// 가격에 불만인 손님이 연관구매로 그냥 사버리는 모순이 생긴다
float UCSPurchaseDecisionComponent::ComputeAffinityBonus(ECSProductCategory TargetCategory, float PriceScore, FString* OutAnchorSummary) const
{
	if (OutAnchorSummary != nullptr)
	{
		OutAnchorSummary->Reset();
	}

	if (PriceScore < PriceReactionNeutralPoint)
	{
		return 0.0f;
	}

	// 서브시스템 3단 체크
	const UWorld* World = GetWorld();
	const UCSCustomerRuntimeSubsystem* RuntimeSubsystem = World != nullptr
		? World->GetSubsystem<UCSCustomerRuntimeSubsystem>()
		: nullptr;
	if (!IsValid(RuntimeSubsystem))
	{
		return 0.0f; // 연관구매가 없던 기존 동작과 동일
	}

	const UCSCustomerStateComponent* State = GetCustomerState();
	if (State == nullptr)
	{
		return 0.0f;
	}

	const TArray<float> Strengths = RuntimeSubsystem->GetAffinityStrengthsForTarget(TargetCategory, State->SelectProducts, OutAnchorSummary);
	if (Strengths.IsEmpty())
	{
		return 0.0f;
	}

	// 같은 대상을 올리는 앵커가 늘어날수록 추가분이 AffinityDiminishingRatio 비율로 줄어듦
	float Bonus = 0.0f;
	float RatioPower = 1.0f;
	for (const float Strength : Strengths)
	{
		Bonus += W_Affinity * Strength * RatioPower;
		RatioPower *= AffinityDiminishingRatio;
	}
	return Bonus;
}

// 구매 성공 여부와 무관하게 후보 하나의 가격 반응 정보를 만든다
// Severity가 0이면(데드존 이내) 기본값(Reason=None)을 그대로 반환해 무반응을 표현
FCSPurchaseRejectionInfo UCSPurchaseDecisionComponent::BuildPriceReactionInfo(const FCSPurchaseCandidate& Candidate) const
{
	FCSPurchaseRejectionInfo Info;

	const FCSProductDataRow* ProductRow = GetProductRow(Candidate);
	if (ProductRow == nullptr)
	{
		return Info; // 데이터 무결성 문제 - 표시 대상 아님
	}

	const FCSPriceEvaluation Eval = EvaluatePrice(Candidate, *ProductRow);
	const float Severity = ComputePriceReactionSeverity(Eval.PriceScore);
	if (Severity <= 0.0f)
	{
		return Info; // 무반응
	}

	Info.Reason = ECSPurchaseRejectReason::PriceRejected;
	Info.ProductId = Candidate.ProductRowHandle.RowName;
	Info.ActualPrice = Eval.ActualPrice;
	Info.ReferencePrice = Eval.ReferencePrice;
	Info.Severity = Severity;
	return Info;
}
