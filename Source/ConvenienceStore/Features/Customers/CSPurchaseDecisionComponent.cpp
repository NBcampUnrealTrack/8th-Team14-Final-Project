#include "Features/Customers/CSPurchaseDecisionComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSCustomerArchetypeDataAsset.h"
#include "Features/Customers/CSCustomerStateComponent.h"
#include "Features/Items/CSProductPickupActor.h"
#include "GameFramework/Actor.h"
#include "Systems/Pricing/CSStorePricingSubsystem.h"
#include "Systems/CustomerFlow/CSCustomerRuntimeSubsystem.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "Systems/NoticeEvents/CSNoticeEventSubsystem.h"
#include "Systems/Weather/CSWeatherSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 가격 반응("비싸다")이 시작되는 선이자 연관구매 보너스를 허용하는 선
	// 두 선이 어긋나면 "비싸다고 느끼는데 연관구매로 사는" 손님이 생긴다
	constexpr float PriceReactionNeutralPoint = 0.5f;

	// 평판 감점이 시작되는 강도. UCSStoreReputationSubsystem::PriceUpsetLowThreshold와 같아야 한다
	constexpr float PriceUpsetSeverityFloor = 0.4f;

	// 뉴스보다 가격 연출을 우선할지. 뉴스에 가려지면 같은 구조체로 판정되는 평판 감점까지 사라진다
	bool PriceOutranksNews(const FCSPurchaseRejectionInfo& Info)
	{
		return Info.Reason == ECSPurchaseRejectReason::PriceRejected
			&& Info.Severity >= PriceUpsetSeverityFloor;
	}

	// 같은 상품을 한 손님이 이만큼 담으면 더 담지 않는다.
	// 제한이 없으면 앞 손님 몇 명이 인기 상품을 쓸어 가서 뒤 손님이 전부 "없네"로 돌아간다
	constexpr int32 MaxSameProductPerVisit = 2;

	int32 CountInBasket(FName ProductId, const UCSCustomerStateComponent* State)
	{
		int32 Count = 0;
		if (State != nullptr)
		{
			for (const FCSSelectProduct& Selected : State->SelectProducts)
			{
				if (Selected.ProductRow.RowName == ProductId)
				{
					++Count;
				}
			}
		}
		return Count;
	}

	// 숙취해소제는 술을 산 손님만 산다. 술 없이 숙취해소제만 사 가는 손님은 어색하다
	bool IsMissingRequiredAnchor(ECSProductCategory Category, const UCSCustomerStateComponent* State)
	{
		if (Category != ECSProductCategory::HangoverCure)
		{
			return false;
		}
		if (State != nullptr)
		{
			for (const FCSSelectProduct& Selected : State->SelectProducts)
			{
				const FCSProductDataRow* Row = Selected.ProductRow.GetRow<FCSProductDataRow>(TEXT("IsMissingRequiredAnchor"));
				if (Row != nullptr && Row->Category == ECSProductCategory::Alcohol)
				{
					return false;
				}
			}
		}
		return true;
	}
}

UCSPurchaseDecisionComponent::UCSPurchaseDecisionComponent()
{
	// 매 프레임 계산이 필요한 로직이 아니라 손님이 진열대 앞에 섰을 때만 호출되면 되므로 Tick은 꺼두기
	PrimaryComponentTick.bCanEverTick = false;

	RandomStream.Initialize(FMath::Rand());

	// 잎 우산은 내구재라 한 방문에 하나면 충분하다. 에디터에서 목록을 늘릴 수 있다
	SingleUnitProductIds.Add(TEXT("SUP_RainUmbrella"));
	// 도구는 매장에서 가장 비싼 상품이라 여러 개 담으면 하루 매출이 한 손님에게 쏠린다
	SingleUnitProductIds.Add(TEXT("TOOL_ArtisanGatheringShears"));
	SingleUnitProductIds.Add(TEXT("TOOL_ArtisanPickaxe"));
	SingleUnitProductIds.Add(TEXT("TOOL_DwarvenMultiTool"));

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

FText UCSPurchaseDecisionComponent::GetEffectivePreferredCategoryMissingTemplate() const
{
	return Archetype ? Archetype->PreferredCategoryMissingTemplate : FallbackPreferredCategoryMissingTemplate;
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

	// 이미 하나 담은 내구재는 더 담지 않는다
	// OutRejectionInfo를 채우지 않는 이유: 거절 연출 대상이 아니라 조용한 미구매다
	// (ScoreAndSelectTopCandidates가 Reason=None인 -1.0 후보를 대표 사유 집계에서 빼주므로
	//  이 후보 때문에 엉뚱한 거절 문구가 뜨지 않는다)
	if (IsSingleUnitLimitReached(Candidate.ProductRowHandle) ||
		CountInBasket(Candidate.ProductRowHandle.RowName, GetCustomerState()) >= MaxSameProductPerVisit)
	{
		return -1.0f;
	}

	// 위와 같이 거절 사유를 안 채운다. 카테고리 차단으로 채우면 그 말풍선이 최우선이라
	// 같은 매대에서 술이 비싸 안 산 경우에도 "비싸다" 대신 그 말풍선이 뜬다
	if (IsMissingRequiredAnchor(ProductRow->Category, GetCustomerState()))
	{
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
	const float AdjustedCategoryScore = ComputeRepeatAdjustedCategoryScore(*ProductRow, CategoryScore);

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
	const float Normalized = ComputeNormalizedScore(PriceScore, AdjustedCategoryScore, BudgetScore);

	// 보너스 항은 여기(모든 -1.0 반환을 통과한 뒤, 기존 Clamp 안)에서만 더한다
	// 이보다 앞에 두면 구매 불가 판정을 받았어야 할 상품이 보너스로 되살아날 수 있다
	const float AffinityBonus = ComputeAffinityBonus(ProductRow->Category, PriceScore);

	// 뉴스 보정은 음수일 수 있지만 아래 Clamp에서 0으로 잘릴 뿐 -1.0(구매 불가)이 되지는 않는다
	const float NewsBonus = ComputeNewsDemandBonus(Candidate.ProductRowHandle);

	// 날씨 보정도 마찬가지. "맑은 날의 우산"처럼 음수가 기본값인 규칙이 많지만
	// 여기서 구매 불가로 바꾸지는 않는다 - 추첨에서 빼는 일은 ScoreAndSelectTopCandidates가 한다
	const float WeatherBonus = ComputeWeatherDemandBonus(Candidate.ProductRowHandle);

	// Clamp는 모두 더한 뒤 한 번만 - 항마다 자르면 음수 보정이 양수 보너스에 묻힌다
	return FMath::Clamp(Normalized + AffinityBonus + NewsBonus + WeatherBonus, 0.f, 1.f);
}

// 전달받은 후보 전체에 점수를 매긴 뒤 점수가 높은 순으로 정렬하고
// 상위 몇개(Archetype의 TopCandidateCount)만 남겨서 반환
// 여기서 잘려나간 후보들은 이후 softmax 선택 대상에서 아예 제외됨
// 살아남은 후보가 하나도 없으면 OutBestRejectionInfo에 대표 거절 사유를 채운다
// 뉴스 보정만 아니었으면 샀을 후보인지 판정
// 뉴스가 수요를 올리는 쪽이거나, 뉴스를 맞고도 기준선을 넘으면 대상이 아니다
bool UCSPurchaseDecisionComponent::TryBuildNewsSuppressionInfo(const FCSPurchaseCandidate& Candidate, float ActualScore, FCSPurchaseRejectionInfo& OutInfo) const
{
	if (ActualScore >= GetEffectiveMinPurchaseUtility())
	{
		return false;
	}

	if (ComputeNewsDemandBonus(Candidate.ProductRowHandle) >= 0.f)
	{
		return false;
	}

	float ScoreWithoutNews = 0.f;
	const FCSPurchaseRejectionInfo Info = BuildScoreShortfallRejectionInfo(Candidate, &ScoreWithoutNews);
	if (Info.Reason != ECSPurchaseRejectReason::NewsDemandRejected)
	{
		return false;
	}

	OutInfo = Info;
	return true;
}

TArray<FCSPurchaseCandidate> UCSPurchaseDecisionComponent::ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates, FCSPurchaseRejectionInfo& OutBestRejectionInfo, FCSPurchaseRejectionInfo& OutNewsSuppressedInfo) const
{
	// 후보 하나하나에 점수를 매기되, 구매 불가 신호(-1.0)를 받은 후보는
	// 애초에 배열에 넣지 않음 → softmax 대상에서 아예 제외되어 선택 확률이 정확히 0%가 됨
	TArray<FCSPurchaseCandidate> Viable;
	Viable.Reserve(Candidates.Num());

	// 대표 거절 사유 집계용. Severity가 가장 낮은(=가장 아깝게 놓친) PriceRejected/HardWalletExceeded를 대표로 삼음
	bool bFoundAnyRejection = false;

	// 뉴스로 밀려난 후보가 여럿이면 가장 높은 점수(=가장 아깝게 놓친 것)를 대표로 삼는다
	float BestSuppressedScore = -1.f;

	for (FCSPurchaseCandidate& Candidate : Candidates)
	{
		FCSPurchaseRejectionInfo CandidateRejection;
		const float Score = ScoreCandidate(Candidate, CandidateRejection);

		// 아래 날씨 억제 판정에 쓰는 원시 수치(가중치를 곱하기 전 값)
		const float WeatherModifier = GetWeatherDemandModifier(Candidate.ProductRowHandle);

		if (Score < 0.f)
		{
			// 상품 데이터를 못 찾아 사유가 안 채워진(Reason=None) 후보는 Severity 기본값(0.0f)이
			// 항상 최솟값이라 아무 사유도 없는 후보가 대표로 뽑힌다
			// 카테고리 차단은 띄우지 않는 사유라 대표로 삼으면 같은 진열대의 가격 반응까지 가려진다
			if (CandidateRejection.Reason == ECSPurchaseRejectReason::None ||
				CandidateRejection.Reason == ECSPurchaseRejectReason::CategoryBlocked)
			{
				continue;
			}

			if (!bFoundAnyRejection || CandidateRejection.Severity < OutBestRejectionInfo.Severity)
			{
				OutBestRejectionInfo = CandidateRejection;
			}
			bFoundAnyRejection = true;
			continue;
		}
		Candidate.Score = Score;

		// 뉴스로 기준선 아래로 떨어진 후보는 여기서 걸러낸다
		// 남겨두면 softmax가 확률로 뽑아버려서 "뉴스 때문에 안 샀다"고 해놓고 사는 일이 생긴다
		FCSPurchaseRejectionInfo NewsInfo;
		if (TryBuildNewsSuppressionInfo(Candidate, Score, NewsInfo))
		{
			if (Score > BestSuppressedScore)
			{
				OutNewsSuppressedInfo = NewsInfo;
				BestSuppressedScore = Score;
			}
			continue;
		}

		// 날씨 보정이 음수인 채로 기준선 아래로 떨어진 후보는 추첨에서 뺀다
		// 점수만 깎고 남겨두면, 1등이 기준선을 넘었을 때 softmax가 이 후보를 확률로 뽑아
		// "비도 안 오는데 우산을 사는" 손님이 계속 나온다
		//
		// 여기서 "날씨만 아니었으면 통과했을까"를 따지지 않는 이유:
		// 가루류는 BaseDemand가 낮아 날씨와 무관하게 원래 기준선 아래일 수 있어서,
		// 원인 귀속형 조건으로는 한 번도 걸리지 않는다. 날씨는 전용 거절 연출이 없으므로
		// 뉴스처럼 "무엇 때문에 안 샀는지"를 정확히 가릴 필요도 없다
		//
		// 거절 정보를 모으지 않는 것도 같은 이유다(띄울 연출이 없다)
		// W_WeatherDemand=0이면 보정이 정확히 0이라 이 조건이 절대 성립하지 않고 기존 동작이 그대로 보존된다
		if (W_WeatherDemand * WeatherModifier < 0.f && Score < GetEffectiveMinPurchaseUtility())
		{
			continue;
		}

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
// 항을 걷어내 "그것만 없었으면 샀을까?"를 차례로 물어 진짜 원인을 가른다
//   0) 가격 불만이 평판 감점 구간인가  → 가격 (뉴스에 가려지면 감점까지 사라져 먼저 본다)
//   1) 뉴스 보정을 빼면 기준선을 넘는가 → 뉴스
//   2) 반복구매 감쇠를 걷어내면 넘는가  → 질린 것이라 연출하지 않음 (Reason=None)
//   3) 둘을 함께 걷어내야 넘는가       → 더 크게 깎은 쪽에 귀속
//   4) 남은 가격 불만이라도 있는가     → 있으면 가격, 없으면 조용히 미구매
// 4)를 조건 없는 else로 두면 카테고리 선호가 낮아 안 산 상품에까지 가격 불만이 붙는다
//
// 전제: GetProductDemandModifier는 뉴스+기념일 합산이고 현재 기념일 규칙은 전부 양수라
// 수요 보정 때문에 못 산 원인은 오늘의 뉴스뿐이다
// 음수 기념일이 생기면 이 전제가 깨지고, 그 경우 기본 문구 행으로 떨어진다
FCSPurchaseRejectionInfo UCSPurchaseDecisionComponent::BuildScoreShortfallRejectionInfo(const FCSPurchaseCandidate& Candidate, float* OutScoreWithoutNews) const
{
	FCSPurchaseRejectionInfo Info;

	const FCSProductDataRow* ProductRow = GetProductRow(Candidate);
	if (ProductRow == nullptr)
	{
		return Info; // 데이터 무결성 문제 - 표시 대상 아님(Reason=None 기본값 그대로)
	}

	const FCSPriceEvaluation PriceEval = EvaluatePrice(Candidate, *ProductRow);

	// 반복구매 감쇠를 걷어낸(RepeatCount=0 가정) 원래 카테고리 점수
	// CategoryScore/BudgetScore가 여기서 -1일 수 없음 - 그랬다면 ScoreCandidate가 그 단계에서
	// 이미 -1.0f를 반환해 이 함수(정상 0~1 점수 케이스 전용)까지 오지도 못했을 것이기 때문
	const float RawCategoryScore = ComputeCategoryScore(*ProductRow);

	// 같은 DecidePurchase 안에서 연달아 호출되고 그 사이에 장바구니가 안 바뀌므로
	// ScoreCandidate가 본 값과 같다
	const float AdjustedCategoryScore = ComputeRepeatAdjustedCategoryScore(*ProductRow, RawCategoryScore);

	float BudgetScore = 1.0f;
	if (UCSCustomerStateComponent* State = GetCustomerState())
	{
		const float OverrunPenalty = Archetype ? Archetype->BudgetOverrunPenalty : FallbackBudgetOverrunPenalty;
		BudgetScore = ComputeBudgetScore(PriceEval.ActualPrice, State->GetRemainingSoftBudget(), State->SoftBudgetGold, State->GetRemainingHardWallet(), OverrunPenalty);
	}

	// ScoreCandidate와 같은 헬퍼로 같은 보너스를 더한다
	const float AffinityBonus = ComputeAffinityBonus(ProductRow->Category, PriceEval.PriceScore);
	const float NewsBonus = ComputeNewsDemandBonus(Candidate.ProductRowHandle);

	// 날씨 항은 아래 재계산식 전부에 실제 점수와 똑같이 더한다 - 걷어내는 판정 단계를 만들지 않는다
	// 날씨가 음수여서 기준선 아래로 간 후보는 ScoreAndSelectTopCandidates가 이미 추첨에서 빼기 때문에
	// 이 함수까지 오는 미달 후보는 날씨 항이 0 이상이고, 따라서 날씨는 이번 거절의 원인이 아니다
	// 한 식에서라도 빠뜨리면 판정용 점수가 실제보다 낮아져 PriceRejected 오분류(=평판 감점)가 생긴다
	const float WeatherBonus = ComputeWeatherDemandBonus(Candidate.ProductRowHandle);

	// 네 가지 가정 점수. 클램프를 걸지 않는 이유는 기준선 대소는 그대로인데
	// 아래 "얼마나 깎았나" 비교가 하한 0에 잘려 과소평가되기 때문
	const float NormalizedWithRepeat = ComputeNormalizedScore(PriceEval.PriceScore, AdjustedCategoryScore, BudgetScore);
	const float NormalizedWithoutRepeat = ComputeNormalizedScore(PriceEval.PriceScore, RawCategoryScore, BudgetScore);
	const float CommonBonus = AffinityBonus + WeatherBonus;

	const float RawActual = NormalizedWithRepeat + CommonBonus + NewsBonus;
	const float RawWithoutNews = NormalizedWithRepeat + CommonBonus;
	const float RawWithoutRepeat = NormalizedWithoutRepeat + CommonBonus + NewsBonus;
	const float RawWithoutBoth = NormalizedWithoutRepeat + CommonBonus;

	// 호출부(TryBuildNewsSuppressionInfo)의 로그용 - 기존과 같이 클램프한 값으로 넘긴다
	if (OutScoreWithoutNews != nullptr)
	{
		*OutScoreWithoutNews = FMath::Clamp(RawWithoutNews, 0.f, 1.f);
	}

	// 각 항이 점수를 깎은 양. 클수록 이번 미달에 더 크게 기여했다
	const float NewsDrop = RawWithoutNews - RawActual;
	const float RepeatDrop = RawWithoutRepeat - RawActual;

	const float MinUtility = GetEffectiveMinPurchaseUtility();
	const float PriceSeverity = ComputePriceReactionSeverity(PriceEval.PriceScore);

	auto FillPriceRejection = [&Info, &Candidate, &PriceEval, PriceSeverity]()
	{
		Info.Reason = ECSPurchaseRejectReason::PriceRejected;
		Info.ProductId = Candidate.ProductRowHandle.RowName;
		Info.ActualPrice = PriceEval.ActualPrice;
		Info.ReferencePrice = PriceEval.ReferencePrice;
		// 하드컷·구매성공 반응과 같은 PriceScore 기반 - 척도가 같아야 DT 티어 문턱이 일관된다
		Info.Severity = PriceSeverity;
	};

	auto FillNewsRejection = [this, &Info, &Candidate, &PriceEval]()
	{
		Info.Reason = ECSPurchaseRejectReason::NewsDemandRejected;
		Info.ProductId = Candidate.ProductRowHandle.RowName;
		Info.ActualPrice = PriceEval.ActualPrice;
		Info.ReferencePrice = PriceEval.ReferencePrice;
		// 가격이 문제가 아니라는 판정이라 가격 기반 Severity를 쓰지 않음
		// 뉴스 행은 Tier가 하나뿐이라 0이 그대로 그 Tier를 고른다
		Info.Severity = 0.f;

		// 서브시스템을 못 찾으면 NAME_None이 되고, 그때는 기본 문구 행으로 떨어진다
		const UWorld* World = GetWorld();
		const UCSNoticeEventSubsystem* NoticeSubsystem = World != nullptr
			? World->GetSubsystem<UCSNoticeEventSubsystem>()
			: nullptr;
		if (IsValid(NoticeSubsystem))
		{
			Info.NewsRowName = NoticeSubsystem->GetTodayNewsRowName();
		}
	};

	const TCHAR* Branch = TEXT("무음-기타");

	if (PriceSeverity >= PriceUpsetSeverityFloor)
	{
		FillPriceRejection();
		Branch = TEXT("가격우선");
	}
	else if (RawWithoutNews >= MinUtility)
	{
		// "뉴스가 있을 때만" 조건은 불필요 - 보정이 0 이상이면 빼도 점수가 안 오른다
		FillNewsRejection();
		Branch = TEXT("뉴스");
	}
	else if (RawWithoutRepeat >= MinUtility)
	{
		// 질린 것뿐이라 연출하지 않는다(Reason=None 유지)
		Branch = TEXT("무음-질림");
	}
	else if (RawWithoutBoth >= MinUtility)
	{
		// 한 항만으로는 설명이 안 되는 복합 원인 - 더 크게 깎은 쪽에 귀속
		if (NewsDrop >= RepeatDrop)
		{
			FillNewsRejection();
			Branch = TEXT("복합-뉴스우세");
		}
		else
		{
			Branch = TEXT("복합-질림우세");
		}
	}
	else if (PriceSeverity > 0.f)
	{
		FillPriceRejection();
		Branch = TEXT("가격잔여");
	}
	// else: 셋 다 원인이 아님(카테고리 선호가 낮은 등) - 띄울 문구가 없어 조용히 미구매

	// 같은 사유가 여러 분기에서 나와 화면만으로는 구분이 안 된다
	// 예비 판정(OutScoreWithoutNews 지정)도 찍는 이유: 여기서 걸러진 후보는 최종 호출이 오지 않는다
	UE_LOG(LogTemp, Log,
		TEXT("[거절귀속] 시드=%d 호출=%s 상품=%s 판정=%s 사유=%s 가격강도=%.2f 실제=%.2f 뉴스뺌=%.2f 감쇠뺌=%.2f 둘다뺌=%.2f 기준선=%.2f"),
		GetCustomerSeed(),
		OutScoreWithoutNews != nullptr ? TEXT("예비") : TEXT("최종"),
		*Candidate.ProductRowHandle.RowName.ToString(),
		Branch,
		*StaticEnum<ECSPurchaseRejectReason>()->GetNameStringByValue(static_cast<int64>(Info.Reason)),
		PriceSeverity, RawActual, RawWithoutNews, RawWithoutRepeat, RawWithoutBoth, MinUtility);

	return Info;
}

// 후보가 없거나(빈 목록) 1등 점수조차 최소 구매 임계치에 못 미치면
// 구매하지 않고 false를 반환(=손님이 살 게 없어서 빈손으로 지나감)
// 그 외에는 상위 후보들 중 하나를 확률적으로 골라 OutSelected에 채우고 true를 반환
// false를 반환하는 모든 경로에서 OutRejectionInfo에 대표 거절 사유를 채운다
bool UCSPurchaseDecisionComponent::HasWorthwhileCandidateOnShelf(const TArray<FCSPurchaseCandidate>& Candidates) const
{
	const float MinUtility = GetEffectiveMinPurchaseUtility();
	for (const FCSPurchaseCandidate& Candidate : Candidates)
	{
		FCSPurchaseRejectionInfo Unused;
		if (ScoreCandidate(Candidate, Unused) >= MinUtility)
		{
			return true;
		}
	}
	return false;
}

bool UCSPurchaseDecisionComponent::DecidePurchase(const TArray<FCSPurchaseCandidate>& Candidates, FCSPurchaseCandidate& OutSelected, FCSPurchaseRejectionInfo& OutRejectionInfo) const
{

	// 후보가 아예 없으면 미구매 - 진열대 자체가 빈 상태
	if (Candidates.IsEmpty())
	{
		OutRejectionInfo.Reason = ECSPurchaseRejectReason::ShelfEmpty;
		return false;
	}

	// 점수 매기고 상위 후보만 추림 (구매 불가 후보들의 대표 거절 사유는 OutRejectionInfo에 누적됨)
	FCSPurchaseRejectionInfo NewsSuppressedInfo;
	const TArray<FCSPurchaseCandidate> TopCandidates = ScoreAndSelectTopCandidates(Candidates, OutRejectionInfo, NewsSuppressedInfo);

	// 뉴스 반응은 다른 반응보다 우선한다 - 가격 불만은 언제든 다시 볼 수 있지만
	// 뉴스는 그날 하루뿐이라 다른 반응에 가려지면 플레이어가 영영 못 본다
	const bool bHasNewsSuppression = NewsSuppressedInfo.Reason == ECSPurchaseRejectReason::NewsDemandRejected;

	// 진열대에 구매 불가 상품(차단 카테고리/예산 초과)만 있으면 Viable이 비어있을 수 있음
	// 이 방어가 없으면 다음 줄에서 TopCandidates[0] 접근 시 배열 범위 초과로 크래시함
	// OutRejectionInfo는 위 ScoreAndSelectTopCandidates 호출에서 이미 채워져 있음
	if (TopCandidates.IsEmpty())
	{
		// 진열대에 있던 게 전부 뉴스로 밀려난 경우 - 위 호출은 -1.0 후보만 집계하므로 여기서 채운다
		// 단 감점 구간의 가격 거절이면 그쪽을 남긴다 - 뉴스 상품이 섞여 있다는 이유로
		// 무관한 상품의 바가지 신호가 사라지면 안 된다
		if (bHasNewsSuppression && !PriceOutranksNews(OutRejectionInfo))
		{
			OutRejectionInfo = NewsSuppressedInfo;
		}
		return false;
	}

	// 1등 점수가 최소 구매 임계치보다 낮으면 손님이 살 게 없다고 판단하고 지나감
	if (TopCandidates[0].Score < GetEffectiveMinPurchaseUtility())
	{
		// 1등의 미달 원인을 먼저 뽑는다 - 감점 구간의 가격 문제면 다른 상품의 뉴스에 덮이면 안 된다
		const FCSPurchaseRejectionInfo ShortfallInfo = BuildScoreShortfallRejectionInfo(TopCandidates[0]);
		OutRejectionInfo = (bHasNewsSuppression && !PriceOutranksNews(ShortfallInfo))
			? NewsSuppressedInfo
			: ShortfallInfo;
		return false;
	}

	// 상위 후보들 중 softmax 확률로 하나 뽑기
	const int32 SelectedIndex = SoftmaxSelectIndex(TopCandidates);
	OutSelected = TopCandidates[SelectedIndex];

	// 구매에 성공해도 가격이 어느 정도 부담스러웠으면 반응을 남김
	// 뉴스로 밀려난 상품이 있었으면 그쪽을 띄운다 - 다른 걸 집어 들면서 "저건 좀 꺼림칙해"
	// 하는 장면이라, 물건을 샀다는 것과 어긋나지 않는다
	// 단 지금 집은 상품의 가격 반응이 감점 구간이면 양보하지 않는다
	const FCSPurchaseRejectionInfo PriceReactionInfo = BuildPriceReactionInfo(OutSelected);
	OutRejectionInfo = (bHasNewsSuppression && !PriceOutranksNews(PriceReactionInfo))
		? NewsSuppressedInfo
		: PriceReactionInfo;

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
	// GetCategoryAttractionWeights 전용 - ECSProductCategory 전체 값을 순회하기 위한 목록
	// (UENUM 리플렉션으로 순회하지 않고 배열로 나열)
	// 카테고리를 추가/삭제하면 이 배열도 반드시 같이 고칠 것
	const ECSProductCategory AllProductCategories[] = {
		ECSProductCategory::Alcohol, ECSProductCategory::Beverage, ECSProductCategory::FreshFood,
		ECSProductCategory::FrozenFood, ECSProductCategory::Potion, ECSProductCategory::Scroll,
		ECSProductCategory::Snack, ECSProductCategory::Supply, ECSProductCategory::Toiletry,
		ECSProductCategory::Tool, ECSProductCategory::HangoverCure
	};

	// 수요 보정을 가중치 배율로 바꿀 때의 하한
	// 배율이 0이 되면 가중치 총합도 0이 될 수 있고, 그러면 아래 WeightedRandomIndex가
	// 난수를 굴리지 않고 빠져나가 같은 시드인데 그 지점 이후 결과가 전부 밀린다
	// 0.1은 "거의 안 뽑히지만 굴림 횟수는 유지된다"를 만족하는 값
	constexpr float MinPlannedDemandMultiplier = 0.1f;

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

// 스폰 시점에 이번 손님이 "뭔가 사러 왔는지"를 정함
// 반드시 인수로 받은 스트림(스포너의 파생 스트림)만 쓴다 - 멤버 RandomStream을 쓰면 손님 마스터 시퀀스가 밀린다
bool UCSPurchaseDecisionComponent::RollShoppingIntent(FRandomStream& Stream) const
{
	return Stream.FRand() < PlannedPurchaseChance;
}

// 스폰 시점에 이번 방문에서 우선적으로 찾아다닐 계획 상품(1~2개)을 미리 뽑아둠
// PlannedBasketStream을 그대로 이어받아 쓰므로 여기서 몇 번을 더 굴리든 MasterStream 시퀀스에는 영향 없음
TArray<FName> UCSPurchaseDecisionComponent::RollPlannedProducts(FRandomStream& Stream) const
{
	TArray<FName> Result;

	if (!IsValid(ProductMasterDataTable))
	{
		return Result;
	}

	// 후보는 이벤트(뉴스·기념일·날씨)로 수요가 오른 상품뿐이다.
	// 평상시에도 계획을 세우면 "뭘 사러 왔다"는 연출이 매일 떠서 이벤트가 이벤트로 안 보인다
	TArray<FName> Candidates;
	TArray<float> Weights;

	const UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
	if (!IsValid(Crafting))
	{
		// 없어도 계획 상품은 돌지만 꽝이 뽑혀 "못 찾음"이 뜰 수 있다
		static bool bWarnedMissingCrafting = false;
		if (!bWarnedMissingCrafting)
		{
			bWarnedMissingCrafting = true;
			UE_LOG(LogTemp, Warning, TEXT("[계획수요] 제조 서브시스템이 없어 꽝 상품을 계획 후보에서 빼지 못합니다."));
		}
	}

	// 가챠 전용 상품은 플레이어가 채울 방법이 없어서 지금 매대에 있을 때만 사러 온다
	// 제조 전용은 발주는 막혀도 만들어 채울 수 있어서 후보에 둔다. 빼면 비 오는 날 우산 손님이 안 온다
	TSet<FName> StockedProductIds;
	if (const UCSCustomerRuntimeSubsystem* Runtime = World ? World->GetSubsystem<UCSCustomerRuntimeSubsystem>() : nullptr)
	{
		for (const TObjectPtr<AActor>& Shelf : Runtime->GetShelves())
		{
			for (const AActor* ProductActor : Runtime->GetProductsOnShelf(Shelf))
			{
				if (const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ProductActor))
				{
					StockedProductIds.Add(Product->GetProductId());
				}
			}
		}
	}

	for (const TPair<FName, uint8*>& Pair : ProductMasterDataTable->GetRowMap())
	{
		const FCSProductDataRow* Row = reinterpret_cast<const FCSProductDataRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}
		const bool bCraftOnly = IsValid(Crafting) && Crafting->IsCraftOnlyResultProduct(Pair.Key);
		if (!Row->bOrderable && !bCraftOnly && !StockedProductIds.Contains(Pair.Key))
		{
			continue;
		}
		// 꽝은 매대에 거의 없어서 계획 상품으로 뽑히면 "못 찾음"만 뜬다.
		// 난수를 굴리기 전에 빼므로 꽝 행이 없던 때와 굴림 횟수가 같다
		if (IsValid(Crafting) && Crafting->IsJunkResultProduct(Pair.Key))
		{
			continue;
		}
		if (Archetype && Archetype->BlockedCategories.Contains(Row->Category))
		{
			continue;
		}
		// 계획은 장바구니가 빈 스폰 시점에 세우는데, 술이 있어야 사는 상품은 그때 살 수 없다
		if (IsMissingRequiredAnchor(Row->Category, nullptr))
		{
			continue;
		}

		// 음수 보정(뉴스로 기피하게 된 상품)은 계획 대상이 아니다 - 사러 올 이유가 없다
		const float Modifier = GetPlannedDemandModifier(Pair.Key);
		if (Modifier <= 0.0f)
		{
			continue;
		}

		// 선호도 맵에 없는 카테고리는 평균(0.5)으로 취급
		const float* FoundPreference = Archetype ? Archetype->CategoryPreference.Find(Row->Category) : nullptr;
		const float RawPreference = FoundPreference ? *FoundPreference : 0.5f;

		// GetCategoryAttractionWeights와 같은 지수를 적용해 종족 취향을 반영한다 -
		// 이게 없으면 눈 오는 날 모든 종족이 똑같이 방한가루를 사러 온다
		// BaseDemand는 같은 이벤트에 여러 상품이 걸렸을 때 인기 상품 쪽으로 쏠리게 하는 항
		const float Weight = FMath::Pow(FMath::Max(RawPreference, 0.0f), CategoryAttractionExponent)
			* GetPlannedDemandMultiplier(Modifier)
			* FMath::Max(static_cast<float>(Row->BaseDemand), 1.0f);

		Candidates.Add(Pair.Key);
		Weights.Add(Weight);
	}

	if (Candidates.Num() == 0)
	{
		// 오늘은 이벤트가 없거나 이 종족이 전부 차단한 카테고리 - 계획 없이 둘러보기만 한다
		return Result;
	}

	// 개수(1~2)를 이 스트림에서 정함
	const int32 TargetCount = (Stream.FRand() < PlannedProductSecondItemChance) ? 2 : 1;

	for (int32 i = 0; i < TargetCount && Candidates.Num() > 0; i++)
	{
		const int32 PickedIndex = WeightedRandomIndex(Weights, Stream);
		const FName PickedProduct = Candidates[PickedIndex];
		Result.Add(PickedProduct);

		// 같은 상품을 두 번 계획하면 계획이 1개인 것과 다를 바 없어서 후보에서 뺀다
		Candidates.RemoveAt(PickedIndex);
		Weights.RemoveAt(PickedIndex);

		// 가중치를 곱하기 전 원시 수치로 찍는다 - W_PlannedDemand=0 대조군에서도 비교가 되게
		const float PickedModifier = GetPlannedDemandModifier(PickedProduct);
		UE_LOG(LogTemp, Log,
			TEXT("[계획수요] 시드=%d 종족=%s 계획상품=%s 수치=%+.2f 배율=%.2f"),
			GetCustomerSeed(),
			Archetype != nullptr ? *Archetype->DisplayName.ToString() : TEXT("?"),
			*PickedProduct.ToString(),
			PickedModifier,
			GetPlannedDemandMultiplier(PickedModifier));
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

TSet<ECSProductCategory> UCSPurchaseDecisionComponent::GetPreferredCategories() const
{
	TSet<ECSProductCategory> Result;
	if (Archetype == nullptr)
	{
		return Result;
	}

	for (const TPair<ECSProductCategory, float>& Pair : Archetype->CategoryPreference)
	{
		if (Pair.Value >= PreferredCategoryThreshold && !Archetype->BlockedCategories.Contains(Pair.Key))
		{
			Result.Add(Pair.Key);
		}
	}
	return Result;
}

// 확률 굴림을 조건 분기보다 먼저 하는 이유는 다른 Roll* 함수들과 같다 -
// 조건에 따라 굴림 여부가 갈리면 같은 시드인데 조건 차이로 그 뒤 결과가 밀린다
bool UCSPurchaseDecisionComponent::RollPreferredCategoryMissing(
	TFunctionRef<bool(ECSProductCategory)> HasStockAnywhere,
	ECSProductCategory& OutCategory) const
{
	if (!RollChance(PreferredCategoryMissingChance) || Archetype == nullptr)
	{
		return false;
	}

	TArray<ECSProductCategory> MissingCandidates;
	for (const TPair<ECSProductCategory, float>& Pair : Archetype->CategoryPreference)
	{
		if (Pair.Value >= PreferredCategoryThreshold && !HasStockAnywhere(Pair.Key))
		{
			MissingCandidates.Add(Pair.Key);
		}
	}

	if (MissingCandidates.IsEmpty())
	{
		return false;
	}

	OutCategory = MissingCandidates[RandomStream.RandRange(0, MissingCandidates.Num() - 1)];
	return true;
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
		// 원인(날씨·뉴스·기념일)을 먼저 찾는다. 아직 만든 적 없는 제조 상품도 플레이어가
		// 만들어 채울 수 있어서, 원인을 알려 줘야 무엇을 만들지 안다
		RowName = TEXT("PlannedProductMissing");
		{
			const FName CauseRowName = GetPlannedMissingCauseRowName(Info.ProductId);
			if (!CauseRowName.IsNone() &&
				RejectionDisplayDataTable->FindRow<FCSPurchaseRejectionDisplayRow>(CauseRowName, TEXT(""), false) != nullptr)
			{
				RowName = CauseRowName;
			}
		}
		break;
	case ECSPurchaseRejectReason::NewsDemandRejected:
		// 뉴스별 전용 문구가 있으면 그걸, 없으면 기본 문구를 쓴다
		// 전용 행은 선택이라 없는 게 정상이므로 조회 경고는 끔
		RowName = TEXT("NewsDemandRejected");
		if (!Info.NewsRowName.IsNone() &&
			RejectionDisplayDataTable->FindRow<FCSPurchaseRejectionDisplayRow>(Info.NewsRowName, TEXT(""), false) != nullptr)
		{
			RowName = Info.NewsRowName;
		}
		break;
	case ECSPurchaseRejectReason::PreferredCategoryMissing:
		// 문구는 이 행이 아니라 종족별 Archetype 문구(BuildInnerThoughtText 참고)를 쓴다.
		// 여기서는 아이콘만 조회하는 용도로 행을 찾는다
		RowName = TEXT("PreferredCategoryMissing");
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

// 계획 상품을 왜 사러 왔는지에 맞는 DT_PurchaseRejectionDisplay 행 이름을 고른다
// ("눈 와서 사러 왔는데" / "뉴스 보고 사러 왔는데" / "기념일이라 사러 왔는데")
// 계획을 세운 시점의 이유를 따로 저장하지 않고 여기서 다시 물어보는 이유:
// 날씨도 뉴스도 하루 동안 안 바뀌어서 스폰 때와 같은 답이 나온다
FName UCSPurchaseDecisionComponent::GetPlannedMissingCauseRowName(FName ProductId) const
{
	if (ProductId.IsNone() || !IsValid(ProductMasterDataTable))
	{
		return NAME_None;
	}

	FDataTableRowHandle Handle;
	Handle.DataTable = ProductMasterDataTable;
	Handle.RowName = ProductId;

	const float WeatherModifier = GetWeatherDemandModifier(Handle);
	const float NewsModifier = GetNewsDemandModifier(Handle);

	// 둘 다 올렸으면 더 크게 올린 쪽이 문구의 주인이다
	if (WeatherModifier > 0.0f && WeatherModifier >= NewsModifier)
	{
		const UWorld* World = GetWorld();
		const UCSWeatherSubsystem* WeatherSubsystem = World != nullptr
			? World->GetSubsystem<UCSWeatherSubsystem>()
			: nullptr;
		if (!IsValid(WeatherSubsystem))
		{
			return NAME_None;
		}

		switch (WeatherSubsystem->GetCurrentWeather())
		{
		case ECSWeatherType::Rain: return TEXT("PlannedMissing_Rain");
		case ECSWeatherType::Snow: return TEXT("PlannedMissing_Snow");
		case ECSWeatherType::Cold: return TEXT("PlannedMissing_Cold");
		case ECSWeatherType::Heat: return TEXT("PlannedMissing_Heat");
		default: return NAME_None;
		}
	}

	if (NewsModifier > 0.0f)
	{
		const UWorld* World = GetWorld();
		const UCSNoticeEventSubsystem* NoticeSubsystem = World != nullptr
			? World->GetSubsystem<UCSNoticeEventSubsystem>()
			: nullptr;
		if (!IsValid(NoticeSubsystem))
		{
			return NAME_None;
		}

		// 합산은 양수인데 개별 규칙은 전부 음수일 수 없으므로 정상적으로는 항상 찾는다
		ECSNoticeEventSourceType Source = ECSNoticeEventSourceType::News;
		if (NoticeSubsystem->GetStrongestPositiveDemandSource(Handle, Source))
		{
			return Source == ECSNoticeEventSourceType::Holiday
				? TEXT("PlannedMissing_Holiday")
				: TEXT("PlannedMissing_News");
		}
	}

	return NAME_None;
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

bool UCSPurchaseDecisionComponent::PickInnerThoughtLine(const FCSPurchaseRejectionInfo& Info, FText& OutLine, FText& OutEventName) const
{
	if (!IsValid(InnerThoughtLinesDataTable) || Archetype == nullptr)
	{
		return false;
	}

	FString Situation;
	switch (Info.Reason)
	{
	case ECSPurchaseRejectReason::PriceRejected:
		// DT_PurchaseRejectionDisplay의 "화남" 단계 문턱과 같다
		Situation = Info.Severity >= 0.6f ? TEXT("PriceAngry") : TEXT("PriceBurden");
		break;
	case ECSPurchaseRejectReason::PlannedProductMissing:
	{
		Situation = TEXT("PlannedProductMissing");
		const FName CauseRowName = GetPlannedMissingCauseRowName(Info.ProductId);
		const UWorld* World = GetWorld();
		const UCSNoticeEventSubsystem* Notice = World ? World->GetSubsystem<UCSNoticeEventSubsystem>() : nullptr;
		if (CauseRowName == TEXT("PlannedMissing_News") || CauseRowName == TEXT("PlannedMissing_Holiday"))
		{
			// 제목을 못 얻으면 "뉴스 보고"만 남은 어색한 문장이 되므로 기본 상황으로 둔다
			FCSNewsEventDataRow News;
			FCSHolidayEventDataRow Holiday;
			if (CauseRowName == TEXT("PlannedMissing_News") && IsValid(Notice) && Notice->GetTodayNews(News))
			{
				OutEventName = News.Title;
				Situation = CauseRowName.ToString();
			}
			else if (CauseRowName == TEXT("PlannedMissing_Holiday") && IsValid(Notice) && Notice->GetTodayHoliday(Holiday))
			{
				OutEventName = Holiday.Title;
				Situation = CauseRowName.ToString();
			}
		}
		else if (!CauseRowName.IsNone())
		{
			Situation = CauseRowName.ToString();
		}
		break;
	}
	case ECSPurchaseRejectReason::PreferredCategoryMissing:
		Situation = TEXT("PreferredCategoryMissing");
		break;
	default:
		return false;
	}

	// 종족은 Archetype 에셋 이름의 마지막 단어다(DA_Archetype_10s_Goblin → Goblin)
	FString AssetName = Archetype->GetName();
	FString Race;
	if (!AssetName.Split(TEXT("_"), nullptr, &Race, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		Race = AssetName;
	}

	const FName RowName(*FString::Printf(TEXT("%s_%s"), *Situation, *Race));
	const FCSInnerThoughtLinesRow* Row = InnerThoughtLinesDataTable->FindRow<FCSInnerThoughtLinesRow>(RowName, TEXT(""), false);
	if (Row == nullptr || Row->Lines.IsEmpty())
	{
		return false;
	}

	// 연출 전용이라 손님 시드 스트림을 쓰지 않는다. 쓰면 이후 구매 판단이 밀린다
	OutLine = Row->Lines[FMath::RandHelper(Row->Lines.Num())];
	return true;
}

// 종족별 문장 후보가 있으면 그중 하나를, 없으면 DT_PurchaseRejectionDisplay 문구에 자리표시자를 채운다
FText UCSPurchaseDecisionComponent::BuildInnerThoughtText(const FCSPurchaseRejectionInfo& Info) const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("ActualPrice"), FText::AsNumber(FMath::RoundToInt(Info.ActualPrice)));
	Args.Add(TEXT("ReferencePrice"), FText::AsNumber(FMath::RoundToInt(Info.ReferencePrice)));
	Args.Add(TEXT("ProductName"), GetProductDisplayName(Info.ProductId));
	Args.Add(TEXT("Category"), CSGetProductCategoryDisplayName(Info.BlockedCategory));

	FText Line;
	FText EventName;
	if (PickInnerThoughtLine(Info, Line, EventName))
	{
		Args.Add(TEXT("EventName"), EventName);
		return FText::Format(Line, Args);
	}

	// PreferredCategoryMissing은 DT 공용 문구가 아니라 종족별 Archetype 문구를 쓴다
	if (Info.Reason == ECSPurchaseRejectReason::PreferredCategoryMissing)
	{
		return FText::Format(GetEffectivePreferredCategoryMissingTemplate(), Args);
	}

	return FText::Format(GetRejectionDisplayTier(Info).InnerThoughtTemplate, Args);
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

// 오늘의 뉴스·기념일 수요 보정 원시 수치
// ComputeAffinityBonus와 달리 가격 게이트를 두지 않음 - 가격이 싸졌다고 기사가 없던 일이 되지는 않는다
float UCSPurchaseDecisionComponent::GetNewsDemandModifier(const FDataTableRowHandle& ProductRow) const
{
	// 서브시스템 3단 체크 - 없으면 보정 0(뉴스 도입 전과 같은 동작)
	const UWorld* World = GetWorld();
	const UCSNoticeEventSubsystem* NoticeSubsystem = World != nullptr
		? World->GetSubsystem<UCSNoticeEventSubsystem>()
		: nullptr;
	if (!IsValid(NoticeSubsystem))
	{
		return 0.0f;
	}

	return NoticeSubsystem->GetProductDemandModifier(ProductRow);
}

float UCSPurchaseDecisionComponent::ComputeNewsDemandBonus(const FDataTableRowHandle& ProductRow) const
{
	return W_NewsDemand * GetNewsDemandModifier(ProductRow);
}

// 오늘 날씨의 상품 수요 보정 원시 수치
// 뉴스와 마찬가지로 가격 게이트를 두지 않는다 - 값이 싸도 비가 안 오면 우산은 필요 없다
float UCSPurchaseDecisionComponent::GetWeatherDemandModifier(const FDataTableRowHandle& ProductRow) const
{
	// 서브시스템 3단 체크 - 없으면 보정 0(날씨 연동 전과 같은 동작)
	const UWorld* World = GetWorld();
	const UCSWeatherSubsystem* WeatherSubsystem = World != nullptr
		? World->GetSubsystem<UCSWeatherSubsystem>()
		: nullptr;
	if (!IsValid(WeatherSubsystem))
	{
		return 0.0f;
	}

	return WeatherSubsystem->GetProductWeatherDemandModifier(ProductRow);
}

float UCSPurchaseDecisionComponent::ComputeWeatherDemandBonus(const FDataTableRowHandle& ProductRow) const
{
	return W_WeatherDemand * GetWeatherDemandModifier(ProductRow);
}

bool UCSPurchaseDecisionComponent::IsSingleUnitLimitReached(const FDataTableRowHandle& ProductRow) const
{
	if (!SingleUnitProductIds.Contains(ProductRow.RowName))
	{
		return false;
	}

	// 손님 상태를 못 찾으면 장바구니를 확인할 수 없으므로 막지 않는다(기존 동작 유지)
	const UCSCustomerStateComponent* State = GetCustomerState();
	if (State == nullptr)
	{
		return false;
	}

	for (const FCSSelectProduct& Selected : State->SelectProducts)
	{
		if (Selected.ProductRow.RowName == ProductRow.RowName)
		{
			return true;
		}
	}

	return false;
}

// 계획 상품 추첨용 수요 보정 = 뉴스 + 날씨
// 둘을 합산하는 이유: 같은 상품에 둘 다 걸릴 수 있고, 한쪽만 반영하면
// "진열대 앞에서는 둘 다 보는데 사러 올 때는 하나만 본다"가 되어 두 축의 처리가 어긋난다
float UCSPurchaseDecisionComponent::GetPlannedDemandModifier(FName ProductRowName) const
{
	if (!IsValid(ProductMasterDataTable) || ProductRowName.IsNone())
	{
		return 0.0f;
	}

	// 추첨 단계에는 상품 액터가 없어서 핸들이 없다. 마스터 테이블 + 행 이름으로 직접 만든다
	FDataTableRowHandle Handle;
	Handle.DataTable = ProductMasterDataTable;
	Handle.RowName = ProductRowName;

	// 뉴스, 기념일, 날씨가 한 상품에 다 겹치면 손님 대부분이 그 상품만 사러 와서 바로 품절되고
	// 못 찾은 손님이 쇼핑을 그만둔다. 올리는 쪽만 막고 내리는 쪽은 그대로 둔다
	constexpr float MaxPlannedDemandModifier = 1.5f;
	return FMath::Min(GetNewsDemandModifier(Handle) + GetWeatherDemandModifier(Handle), MaxPlannedDemandModifier);
}

float UCSPurchaseDecisionComponent::GetPlannedDemandMultiplier(float DemandModifier) const
{
	return FMath::Max(1.0f + W_PlannedDemand * DemandModifier, MinPlannedDemandMultiplier);
}

// 반복구매 감쇠를 적용한 카테고리 점수
// 채점과 거절 원인 판정이 같은 값을 봐야 해서 함수로 뽑아둠
// 복사본을 두면 한쪽만 고쳤을 때 "왜 안 샀는가"의 답이 실제 점수와 어긋난다
float UCSPurchaseDecisionComponent::ComputeRepeatAdjustedCategoryScore(const FCSProductDataRow& ProductRow, float CategoryScore) const
{
	const UCSCustomerStateComponent* State = GetCustomerState();
	if (State == nullptr)
	{
		return CategoryScore;
	}

	int32 RepeatCount = 0;
	for (const FCSSelectProduct& Selected : State->SelectProducts)
	{
		const FCSProductDataRow* SelectedRow = Selected.ProductRow.GetRow<FCSProductDataRow>(
			TEXT("UCSPurchaseDecisionComponent::ComputeRepeatAdjustedCategoryScore"));
		if (SelectedRow != nullptr && SelectedRow->Category == ProductRow.Category)
		{
			RepeatCount++;
		}
	}

	if (RepeatCount <= 0)
	{
		return CategoryScore;
	}

	return CategoryScore * FMath::Pow(GetEffectiveRepeatCategoryPenalty(), static_cast<float>(RepeatCount));
}

// 보너스를 더하기 전의 정규화 총점
// 두 점수를 서로 비교하는 용도라 채점과 판정이 같은 식을 써야 한다
float UCSPurchaseDecisionComponent::ComputeNormalizedScore(float PriceScore, float CategoryScore, float BudgetScore) const
{
	const float Weighted = (W_Price * PriceScore) + (W_Category * CategoryScore) + (W_Budget * BudgetScore);
	const float WeightSum = W_Price + W_Category + W_Budget;
	return (WeightSum > 0.f) ? (Weighted / WeightSum) : 0.f;
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
