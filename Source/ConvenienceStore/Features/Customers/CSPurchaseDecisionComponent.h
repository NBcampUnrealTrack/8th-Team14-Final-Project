#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "Math/RandomStream.h"
#include "CSPurchaseDecisionComponent.generated.h"

struct FCSProductDataRow;
class UCSCustomerArchetypeDataAsset;
class UCSCustomerStateComponent;

// 손님이 무엇을 살지 판단하는 로직을 담는 컴포넌트

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CONVENIENCESTORE_API UCSPurchaseDecisionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSPurchaseDecisionComponent();

	// 후보 목록 중 하나를 확률적으로 골라 OutSelected에 채움
	// 반환값이 true면 구매 결정 false면 살 만한 게 없어서 미구매
	// OutRejectionInfo는 이제 성패와 무관하게 항상 채워짐(Reason=None이면 무반응) - 구매에 성공해도
	// 가격이 어느 정도 부담스러웠으면(Severity>0) 가벼운 반응이 여기 담김
	// false일 때는 기존 그대로 거절 사유(카테고리 차단/하드지갑 초과/재고없음/가격)가 담김
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	bool DecidePurchase(const TArray<FCSPurchaseCandidate>& Candidates, FCSPurchaseCandidate& OutSelected, FCSPurchaseRejectionInfo& OutRejectionInfo) const;

	// 이 손님이 어떤 종족 성향을 갖는지
	// 스폰 시점에 채워지는 것을 전제로
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Purchase|Archetype")
	TObjectPtr<UCSCustomerArchetypeDataAsset> Archetype;

	// 계획 상품(RollPlannedProducts)을 뽑을 때 카테고리별 후보 수집·수요 가중치 조회에 쓰는
	// 상품 마스터 데이터테이블
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Planned")
	TObjectPtr<UDataTable> ProductMasterDataTable;

	// 지금까지 담은 개수를 보고 하나 더 담을지를 0~1 확률로 계산
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	float ContinueShoppingProbability(int32 ItemsInBasket) const;

	// 담은 개수 → 확률 계산 + 실제 굴림까지 한 번에 처리
	// 상품을 담은 직후 CS_EvaluateSessionContinuation(BrowseShelf 세션 지속 판단)에서 라운드당 1회 호출됨
	// 호출부는 내부 확률/난수를 몰라도 됨
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	bool RollContinueShopping(int32 ItemsInBasket) const;

	// 스폰 시점에 이번 방문에서 목표로 삼을 장바구니 개수를 미리 뽑아둠
	int32 RollPlannedBasketTarget(FRandomStream& Stream) const;

	// 이번 손님이 "뭔가 사러 왔는지"를 정함(PlannedPurchaseChance 1회 굴림)
	// 입장 확률 구간(계획/무계획)을 가르는 값이며 아래 계획 상품 목록과는 별개다 -
	// 이벤트가 없는 날은 사러 온 손님이어도 목록이 비어 있고, 그때 둘을 같은 값으로 묶으면
	// 평상시 전원이 무계획 취급돼 입장률이 통째로 무너진다
	bool RollShoppingIntent(FRandomStream& Stream) const;

	// 스폰 시점에 이번 방문에서 우선적으로 찾아다닐 계획 상품(1~2개)을 미리 뽑아둠
	// 후보는 이벤트(뉴스·기념일·날씨)로 수요가 오른 상품뿐이라 평상시에는 빈 배열이 나온다
	// 개수 자체도 이 스트림에서 굴려서 정함(PlannedProductSecondItemChance)
	// 매장 해금 레벨은 보지 않는다 - 아직 못 파는 물건을 손님이 찾는 것 자체가
	// "저걸 해금해야겠다"는 신호라서 의도된 동작이다
	TArray<FName> RollPlannedProducts(FRandomStream& Stream) const;

	// 카테고리 선호를 손님 동선(PickNextShelf가 다음에 어느 진열대로 갈지 고르는 것)에 반영하기 위한
	// 카테고리별 끌림 가중치
	TMap<ECSProductCategory, float> GetCategoryAttractionWeights() const;

	// PreferredCategoryThreshold 이상 카테고리. PickNextShelf가 연관구매 대상과 합쳐 하드 필터로 씀
	TSet<ECSProductCategory> GetPreferredCategories() const;

	// 퇴장 시점 전용: 계획구매와 무관하게, 이 손님이 좋아하는 카테고리(PreferredCategoryThreshold 이상) 중
	// 매장에 재고가 하나도 없는 것을 하나 고른다. PreferredCategoryMissingChance 확률 굴림까지 여기서 처리하므로
	// 실패(확률에서 떨어지거나 해당 카테고리가 없음)하면 false.
	// HasStockAnywhere는 "이 카테고리가 지금 매장 어딘가에 재고로 남아있는가"를 판정하는 콜백 -
	// 매대 순회는 손님 개인 데이터가 아니라 서브시스템(RuntimeSubsystem) 소관이라 호출부가 넘겨준다
	bool RollPreferredCategoryMissing(TFunctionRef<bool(ECSProductCategory)> HasStockAnywhere, ECSProductCategory& OutCategory) const;

	// 스포너가 스폰 직후 파생 시드를 주입할 때 사용
	// 이미 굴림이 시작된 뒤에 다시 부르면 그 시점부터 시퀀스가 바뀌므로 스폰 직후 1회만 호출 전제
	UFUNCTION(BlueprintCallable, Category = "Purchase|Random")
	void InitializeRandomStream(int32 Seed);

	// 0~1 확률을 받아 이 손님의 스트림으로 굴립니다.
	// 가게로 들어갈지 안갈지를 정할려고 확률을 굴리는 함수입니다.
	UFUNCTION(BlueprintCallable, Category = "Purchase|Random")
	bool RollChance(float Chance) const;

	// FRandomStream은 원본 시드와 진행 상태를 따로 들고 있어서 이 getter를 몇 번 읽어도 난수 순서에 영향이 없다
	// 로그에서 같은 시드 실행끼리 손님을 맞춰보기 위한 용도
	int32 GetCustomerSeed() const { return RandomStream.GetInitialSeed(); }

	// Archetype이 있으면 그 값을, 없으면 폴백값을 쓰는 헬퍼
	// 원래 private이었으나 FCSTask_BrowseShelf::Tick에서 이번 방문에 실제로 몇 개를 비교 대상에 올렸는지
	// (= 평가 예산 차감량)를 계산하려면 컴포넌트 밖에서 이 값을 읽어야 해서 public으로 옮김
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	int32 GetEffectiveTopCandidateCount() const;

	// Candidates 중 MinPurchaseUtility 이상이 있는지 (RNG 미사용). 진열대 체류/이동 판정에 씀
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	bool HasWorthwhileCandidateOnShelf(const TArray<FCSPurchaseCandidate>& Candidates) const;

	// =====================================================================
	// 거절 연출 (인지 채널)
	// =====================================================================

	// 거절 즉시표시(디버그 텍스트) 대기시간. 마법봉 시간 게이트도 이 값을 그대로 참조하므로
	// 두 곳에 따로 상수를 두지 말고 항상 이 getter를 거칠 것(Reason 하나로 두 곳이 항상 같은 값을 보게 함)
	// ShelfEmpty만 별도로 짧은 값을 씀 - 재고가 없어서 지나가는 것뿐인데 5초씩 붙잡아두면 너무 길다
	// 가격 반응도 실제 거절과 똑같이 손님을 멈춰 세운 뒤 담기/안 담기가
	// 갈리는 구조라 이 값을 그대로 같이 씀
	UFUNCTION(BlueprintCallable, Category = "Purchase|Rejection")
	float GetInstantDisplayDuration(ECSPurchaseRejectReason Reason) const
	{
		return Reason == ECSPurchaseRejectReason::ShelfEmpty ? EmptyShelfDisplayDuration : InstantDisplayDuration;
	}

	// 손님이 멈춰 서 있는 시간. GetInstantDisplayDuration(아이콘/마법봉 표시시간)과 분리 -
	// 손님까지 그만큼 얼어있으면 부자연스럽다
	UFUNCTION(BlueprintCallable, Category = "Purchase|Rejection")
	float GetReactionHoldDuration(ECSPurchaseRejectReason Reason) const
	{
		return Reason == ECSPurchaseRejectReason::ShelfEmpty ? EmptyShelfDisplayDuration : ReactionHoldSeconds;
	}

	// Info.Reason에 대응하는 DT 행을 찾아 Severity에 맞는 Tier를 반환
	// 행이 없거나 Tiers가 비어있으면 빈 Tier(전부 기본값)를 반환
	UFUNCTION(BlueprintCallable, Category = "Purchase|Rejection")
	FCSPurchaseRejectionTier GetRejectionDisplayTier(const FCSPurchaseRejectionInfo& Info) const;

	// 위 Tier의 InnerThoughtTemplate에 실제 가격을 채워 완성된 문장을 반환
	UFUNCTION(BlueprintCallable, Category = "Purchase|Rejection")
	FText BuildInnerThoughtText(const FCSPurchaseRejectionInfo& Info) const;

protected:
	// 폴백 전용 (Archetype이 있으면 이 값들은 무시)
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	int32 FallbackTopCandidateCount = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackSelectionTemperature = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackMinPurchaseUtility = 0.35f;

	// 10~15% 데드존 확대안의 중간값
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackAcceptanceLatitude = 0.12f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackPriceElasticity = 2.62f;

	// 가격 하드컷 임계값 폴백값 — Archetype 없을 때만 사용
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackPriceRejectionThreshold = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	int32 FallbackBudgetMedianGold = 380;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackBudgetLogSigma = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackHardWalletRatio = 1.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackBudgetOverrunPenalty = 2.0f;

	// CountDecay(담을수록 하나 더 담을 확률이 지수적으로 줄어드는 계수) 폴백값
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackCountDecayLambda = 0.15f;

	// 같은 카테고리 반복 시 점수 감쇠 배수 폴백값
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackRepeatCategoryPenalty = 0.7f;

	// PreferredCategoryMissing 속마음 문구 폴백값. {Category} 자리표시자가 실제 카테고리명으로 치환됨
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	FText FallbackPreferredCategoryMissingTemplate = FText::FromString(TEXT("{Category} 사러 왔는데 하나도 없네..."));

	// 아래 값은 Archetype(종족별 데이터)이 아니라 판단 로직 자체의 튜닝값이라 컴포넌트에 남겨둠
	// 종족마다 가중치 배분을 다르게 하고 싶어지면 그때 DA로 옮기는 걸 고려

	// 가격 점수가 전체 점수에서 차지하는 가중치 (하드컷 도입으로 극단 바가지는
	// 별도 차단되므로 중간 가격대에서 카테고리 신호가 더 잘 드러나도록 낮춤)
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Price = 2.0f;

	// 카테고리 점수가 전체 점수에서 차지하는 가중치
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Category = 1.5f;

	// 예산 점수가 전체 점수에서 차지하는 가중치
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Budget = 1.0f;

	// 데드존 바깥 기울기 배수
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float PriceSteepnessMultiplier = 1.4f;

	// 카테고리 연관구매 점수 보너스 세기
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Affinity = 0.25f;

	// 오늘의 뉴스·기념일 수요 보정이 점수에 반영되는 세기
	// 뉴스는 종족 성향이 아니라 가게에 걸린 외부 사건이라 종족 차등 없이 컴포넌트 고정값으로 둠
	// 0으로 두면 뉴스 영향이 꺼져서 검증 대조군으로 쓸 수 있음
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_NewsDemand = 0.3f;

	// 오늘 날씨가 상품 수요에 반영되는 세기
	// 뉴스와 같은 이유로 종족 차등을 두지 않는다
	// 0으로 두면 날씨 영향이 완전히 꺼져서 검증 대조군으로 쓸 수 있음
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_WeatherDemand = 0.3f;

	// 뉴스·날씨 수요 보정이 "무엇을 사러 올지"(RollPlannedProducts)에 반영되는 세기
	// 진열대 앞 채점(W_NewsDemand/W_WeatherDemand)과 따로 두는 이유:
	// 채점은 이미 진열대에 도착한 손님의 판단이고, 이쪽은 애초에 그 진열대로 갈지를 정한다.
	// 진열대가 20개를 넘으면 손님이 도는 3곳 안에 날씨 상품 진열대가 들어갈 일이 거의 없어서
	// 채점만으로는 효과가 매장에 드러나지 않는다 - 그 격차를 메우는 값이라 세기가 다를 수밖에 없다
	// 0으로 두면 계획 추첨이 수요 보정을 무시해 연동 전과 같은 동작이 된다(검증 대조군)
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights", meta = (ClampMin = "0.0"))
	float W_PlannedDemand = 2.0f;

	// 이벤트 상품을 사러 온 손님이 지갑을 얼마나 더 챙겨 오는지. 하드 지갑 × (1 + 이 값 × 계획 상품 수요 보정)
	// 소프트 예산은 그대로라 비싸다는 감점은 남는다. 뉴스와 같은 이유로 종족 차등을 두지 않는다
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights", meta = (ClampMin = "0.0"))
	float W_PlannedWalletBoost = 0.5f;

	// 한 손님이 이번 방문에 한 개까지만 담는 상품 (우산처럼 하나 있으면 더 살 이유가 없는 내구재)
	// 가루·음료 같은 소모품은 여러 개 사는 게 자연스러워서 대상이 아니다
	// 반복구매 감쇠(RepeatCategoryPenalty)는 카테고리 단위라 이걸 막지 못한다 -
	// 날씨 보너스가 감쇠를 이기면 우산을 세 개씩 사는 손님이 나온다(실측됨)
	// 본래는 상품 표에 "내구재" 플래그로 있어야 맞지만 DT_ProductMaster는 다른 파트 소관이라
	// 우선 여기 둔다. 상품 표에 필드가 생기면 그쪽으로 옮길 것
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Limits")
	TSet<FName> SingleUnitProductIds;

	// 같은 대상을 끌어올리는 앵커가 여러 개일 때 두 번째부터 추가분을 줄이는 비율
	// 1 미만이면 보너스 상한은 (첫 보너스 ÷ (1 − 비율))
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AffinityDiminishingRatio = 0.5f;

	// 카테고리 선호가 진열대 동선에 얼마나 뚜렷하게 드러날지 조절하는 지수(GetCategoryAttractionWeights에서 사용)
	// 1이면 선호도 차이가 그대로 확률 차이, 클수록 좋아하는 진열대 쪽으로 훨씬 강하게 쏠림
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights", meta = (ClampMin = "0.0"))
	float CategoryAttractionExponent = 2.0f;

	// 거절 즉시표시(디버그 텍스트)를 몇 초간 유지할지. 마법봉 시간 게이트도 같은 값을 참조함
	// 손님마다 달라질 이유가 없는 값이라 Archetype/Fallback 이중화 대상이 아님
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	float InstantDisplayDuration = 12.0f;

	// 손님이 실제로 멈춰 서 있는 시간(초). GetReactionHoldDuration이 참조
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	float ReactionHoldSeconds = 3.5f;

	// ShelfEmpty 전용 즉시표시 대기시간. 재고가 없어서 그냥 지나가는 것뿐인데
	// 다른 거절 사유와 똑같이 5초씩 붙잡아두면 너무 길다
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	float EmptyShelfDisplayDuration = 1.5f;

	// 거절 사유별 문구/강도 구간을 담은 DataTable. 행 구조는 FCSPurchaseRejectionDisplayRow
	// 재컴파일 없이 디자이너가 문장을 조정할 수 있도록 값 전부를 여기로 뺌
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	TObjectPtr<UDataTable> RejectionDisplayDataTable;

	// 종족별 속마음 문장 후보. 행 구조는 FCSInnerThoughtLinesRow
	// 비어 있거나 행이 없으면 RejectionDisplayDataTable 문구를 그대로 쓴다
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	TObjectPtr<UDataTable> InnerThoughtLinesDataTable;

	// 이 손님이 계획 상품을 가질 확률. 실패하면 계획 없이 스폰되어 진열대에서 본 것으로만 산다(충동구매)
	// 전원이 계획을 가지면 입장 판정(ShouldEnterStore)의 "계획 없음" 분기가 항상 비어 효과가 사라지므로
	// 이 굴림으로 두 부류를 실제로 갈라놓는다
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Planned", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlannedPurchaseChance = 0.65f;

	// 계획 상품을 1개가 아니라 2개 뽑을 확률
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Planned", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlannedProductSecondItemChance = 0.35f;

	// 퇴장 시 계획구매 관련 속마음이 안 뜬 "나머지" 손님 중 이 비율만 선호 카테고리 품절 반응을 함.
	// 전원에게 매번 띄우면 이번엔 반대로 매일 뜨는 게 뻔해져서, 이벤트 없는 날의 심심함을 못 없앤다
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Planned", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PreferredCategoryMissingChance = 0.4f;

	// 이 값 이상인 카테고리만 "선호 카테고리"로 취급(0.5=평균이므로 그보다 확실히 높은 지점)
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Planned", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float PreferredCategoryThreshold = 0.65f;

	// 후보 하나에 점수를 매기는 함수
	// 반환값 규약: 정상 후보는 0.0~1.0, 구매 불가(하드 차단/지갑 초과/데이터 없음)는 음수(-1.0)를 반환
	// 구매 불가 시 OutRejectionInfo에 사유를 채운다 성공 시에는 건드리지 않는다(Reason=None으로 남음)
	virtual float ScoreCandidate(const FCSPurchaseCandidate& Candidate, FCSPurchaseRejectionInfo& OutRejectionInfo) const;

	// 점수 계산 함수 (이 상품 가격이 적당한가)
	float ComputePriceScore(float ActualPrice, float ReferencePrice, float DeadZoneRatio, float Steepness) const;

	// 이 종족이 이 상품 카테고리를 좋아하는가를 점수로 바꿔주는 함수
	float ComputeCategoryScore(const FCSProductDataRow& ProductRow) const;

	// 남은 예산 대비 이 가격이 감당 가능한가를 0~1 점수로 계산
	// 하드 지갑을 넘으면 -1.0(무조건 구매 불가 신호)을 반환
	float ComputeBudgetScore(float ActualPrice, int32 RemainingSoftBudget, int32 OriginalSoftBudget, int32 RemainingHardWallet, float OverrunPenalty) const;

private:
	// 후보가 가리키는 상품 마스터 데이터를 데이터테이블에서 조회합니다.
	const FCSProductDataRow* GetProductRow(const FCSPurchaseCandidate& Candidate) const;

	// ProductId(RowName)만으로 화면 표시용 상품명을 조회
	// BuildInnerThoughtText의 {ProductName} 자리표시자 전용
	FText GetProductDisplayName(FName ProductId) const;

	// 계획 상품을 못 찾았을 때, 사러 온 이유(날씨/뉴스/기념일)에 맞는 표시 행 이름
	// 해당하는 이유가 없으면 NAME_None - 호출부가 기본 행으로 돌아간다
	FName GetPlannedMissingCauseRowName(FName ProductId) const;

	// InnerThoughtLinesDataTable에서 이 손님 종족의 문장 하나를 고른다. 없으면 false
	// 뉴스·기념일 상황이면 OutEventName에 오늘 뉴스 제목이나 기념일 이름을 채운다
	bool PickInnerThoughtLine(const FCSPurchaseRejectionInfo& Info, FText& OutLine, FText& OutEventName) const;

	// 후보 하나의 실제가/기준가/PriceScore를 한 번에 구하는 공용 계산 결과
	// ScoreCandidate/BuildScoreShortfallRejectionInfo/BuildPriceReactionInfo 세 곳이
	// 전부 같은 절차(기준가 조회 → 실제가 조회 → 데드존/기울기로 PriceScore 계산)를 반복해서
	// 중복을 없애려고 뽑아냄
	struct FCSPriceEvaluation
	{
		float ActualPrice = 0.f;
		float ReferencePrice = 0.f;
		float PriceScore = 0.5f; // 상품 데이터를 못 찾는 등 계산 불가 시 중립값
	};
	FCSPriceEvaluation EvaluatePrice(const FCSPurchaseCandidate& Candidate, const FCSProductDataRow& ProductRow) const;

	// PriceScore(0=완전 불만족~0.5=중립~1=완전 만족) 하나만으로 "가격에 대한 반응 강도"를
	// 0(무반응)~1(최고 강도)로 환산
	// 구매 성패와 분리된 값 - 손님이 실제로 사든 안 사든 이 값만으로 표시 단계가 갈림
	// 하드컷 문턱(PriceRejectionThreshold)을 [0.6, 1.0] 구간에, 그 위(데드존~하드컷 사이)를
	// [0, 0.6) 구간에 배정해서 화날 정도로 가격이 오름(DT SeverityThreshold 0.6)이 뜨면 항상 실제로도
	// 하드컷(=구매 불가)과 일치하도록 만듦
	float ComputePriceReactionSeverity(float PriceScore) const;

	// 구매에 성공한 후보를 대상으로 가격 반응 정보를 만든다
	// Severity가 0이면(데드존 이내) Reason=None인 기본값을 그대로 반환해 무반응을 표현
	FCSPurchaseRejectionInfo BuildPriceReactionInfo(const FCSPurchaseCandidate& Candidate) const;

	// 카테고리 연관구매 점수 보너스를 계산한다. TargetCategory는 지금 채점 중인 후보의 카테고리
	// (장바구니에 이미 담긴 앵커 카테고리들이 이 카테고리를 끌어올리는가를 본다)
	// PriceScore가 가격 반응 중립점보다 낮으면(비싸다고 느끼는 중이면) 0을 돌려준다 -
	// 비싸서 망설이는 손님이 연관구매로 그냥 사버리는 모순을 막기 위함
	float ComputeAffinityBonus(ECSProductCategory TargetCategory, float PriceScore, FString* OutAnchorSummary = nullptr) const;

	// 오늘의 뉴스·기념일이 이 상품 수요를 얼마나 바꾸는지 (가중치 곱하기 전 원시 수치)
	// 뉴스가 없거나 규칙이 안 걸리면 0
	float GetNewsDemandModifier(const FDataTableRowHandle& ProductRow) const;

	// 위 수치에 W_NewsDemand를 곱한 실제 점수 보정분
	float ComputeNewsDemandBonus(const FDataTableRowHandle& ProductRow) const;

	// 오늘 날씨가 이 상품 수요를 얼마나 바꾸는지 (가중치 곱하기 전 원시 수치)
	// 날씨 서브시스템이 없거나 규칙이 안 걸리면 0
	float GetWeatherDemandModifier(const FDataTableRowHandle& ProductRow) const;

	// 위 수치에 W_WeatherDemand를 곱한 실제 점수 보정분
	float ComputeWeatherDemandBonus(const FDataTableRowHandle& ProductRow) const;

	// 이 상품이 1개 제한 대상이고 이미 장바구니에 들어 있는지
	bool IsSingleUnitLimitReached(const FDataTableRowHandle& ProductRow) const;

	// 계획 상품 추첨에 쓰는 수요 보정 원시 수치 = 뉴스 + 날씨 합산
	// 상품 마스터 행 이름만 받아서 내부에서 핸들을 만든다(추첨 단계에는 핸들이 없고 RowName만 있다)
	float GetPlannedDemandModifier(FName ProductRowName) const;

	// 위 수치를 가중치 배율로 바꾼다. 하한이 있어 0이 되지 않는다
	// 하한이 없으면 가중치 총합이 0이 되는 경우가 생기고, 그때 WeightedRandomIndex가
	// 난수를 굴리지 않고 반환해서 같은 시드인데 그 지점부터 결과가 통째로 밀린다
	float GetPlannedDemandMultiplier(float DemandModifier) const;

	// 장바구니에 담긴 같은 카테고리 개수만큼 카테고리 점수를 지수적으로 깎음
	// 채점과 거절 원인 판정이 같은 값을 봐야 해서 한 곳에 모아둠
	float ComputeRepeatAdjustedCategoryScore(const FCSProductDataRow& ProductRow, float CategoryScore) const;

	// 가격/카테고리/예산 점수를 가중합해 0~1로 정규화 (보너스 더하기 전 단계)
	// 위와 같은 이유로 헬퍼로 빼둠
	float ComputeNormalizedScore(float PriceScore, float CategoryScore, float BudgetScore) const;

	// Archetype 유무에 따라 실제 쓸 값을 결정하는 헬퍼들
	// Archetype이 연결되어 있으면 그 안의 값을 쓰고
	// 없으면 기본값(Fallback)을 대신 쓴다는 판단 해주는 함수들
	float GetEffectiveSelectionTemperature() const;
	float GetEffectiveMinPurchaseUtility() const;
	float GetEffectiveCountDecayLambda() const;
	float GetEffectiveRepeatCategoryPenalty() const;
	float GetEffectivePriceRejectionThreshold() const;
	FText GetEffectivePreferredCategoryMissingTemplate() const;

	// 전체 후보에 점수를 매긴 뒤 점수 내림차순으로 상위 TopCandidateCount개만 남김
	// 살아남은 후보가 하나도 없으면(전부 -1.0) OutBestRejectionInfo에 대표 거절 사유를 채움
	// Severity가 가장 낮은(=가장 아깝게 놓친) 것. CategoryBlocked는 대표로 삼지 않는다
	// 뉴스 때문에 기준선 아래로 떨어진 후보는 여기서 아예 빼고 OutNewsSuppressedInfo에 담아 내보냄
	TArray<FCSPurchaseCandidate> ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates, FCSPurchaseRejectionInfo& OutBestRejectionInfo, FCSPurchaseRejectionInfo& OutNewsSuppressedInfo) const;

	// 뉴스 보정만 아니었으면 샀을 후보인지 판정. 맞으면 OutInfo에 뉴스 거절 정보를 채우고 true
	// 이 후보는 softmax 대상에서 빠진다 - 안 산다고 판정해놓고 확률로 뽑아 사버리면 말이 안 되기 때문
	bool TryBuildNewsSuppressionInfo(const FCSPurchaseCandidate& Candidate, float ActualScore, FCSPurchaseRejectionInfo& OutInfo) const;

	// 상위 후보들 중 softmax 확률로 하나의 인덱스 뽑기
	int32 SoftmaxSelectIndex(const TArray<FCSPurchaseCandidate>& TopCandidates) const;

	// 1등 후보는 있지만 총점이 MinPurchaseUtility에 못 미쳐 거절되는 케이스 전용 헬퍼
	// ScoreCandidate는 이 경우 -1.0을 반환하지 않아(정상 0~1 점수) OutRejectionInfo가 안 채워지므로 여기서 별도로 채움
	// 총점 미달의 진짜 원인이 가격/선호 자체인지, 반복구매로 인한 카테고리 감쇠(RepeatCategoryPenalty)인지
	// 반복구매 감쇠를 걷어내도 기준선을 못 넘었을까?로 재계산해서 구분함
	// 안 그러면 이미 장바구니에 담은 물품을 똑같은 상품인데 갑자기 너무 비싸다고 함 같은 사유 오분류가 생김
	// OutScoreWithoutNews를 넘기면 뉴스 보정을 뺀 점수를 함께 돌려준다(로그용)
	FCSPurchaseRejectionInfo BuildScoreShortfallRejectionInfo(const FCSPurchaseCandidate& Candidate, float* OutScoreWithoutNews = nullptr) const;

	// 이 컴포넌트가 붙어있는 액터에서 CSCustomerStateComponent를 찾아옴
	UCSCustomerStateComponent* GetCustomerState() const;

	// 아직 예산이 안 뽑힌 손님이면 Archetype(또는 폴백값)을 참조해 예산을 한 번 뽑아 채움
	// 이미 초기화된 손님이면 아무것도 하지 않음
	void EnsureBudgetInitialized(UCSCustomerStateComponent& State) const;

	// 이 손님 전용 난수 스트림
	mutable FRandomStream RandomStream;

	// 표준정규분포(평균 0, 표준편차 1) 난수 하나를 뽑음 (Box-Muller 변환)
	// 전역 RNG 대신 RandomStream을 쓰도록 static 제거, 인스턴스 메서드로 전환
	float SampleStandardNormal() const;
};
