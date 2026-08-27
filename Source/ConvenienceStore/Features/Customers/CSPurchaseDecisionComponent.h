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
	// false일 때 OutRejectionInfo에 대표 거절 사유가 채워짐 - 거절 연출이 이 값을 읽음
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	bool DecidePurchase(const TArray<FCSPurchaseCandidate>& Candidates, FCSPurchaseCandidate& OutSelected, FCSPurchaseRejectionInfo& OutRejectionInfo) const;

	// 이 손님이 어떤 종족 성향을 갖는지
	// 스폰 시점에 채워지는 것을 전제로
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Purchase|Archetype")
	TObjectPtr<UCSCustomerArchetypeDataAsset> Archetype;

	// 지금까지 담은 개수를 보고 하나 더 담을지를 0~1 확률로 계산
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	float ContinueShoppingProbability(int32 ItemsInBasket) const;

	// 담은 개수 → 확률 계산 + 실제 굴림까지 한 번에 처리
	// StateTree Condition은 이 함수 하나만 호출하면 되고 내부 확률/난수는 몰라도 됨
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	bool RollContinueShopping(int32 ItemsInBasket) const;

	// 스폰 시점에 이번 방문에서 목표로 삼을 장바구니 개수를 미리 뽑아둠
	int32 RollPlannedBasketTarget(FRandomStream& Stream) const;

	// 스포너가 스폰 직후 파생 시드를 주입할 때 사용
	// 이미 굴림이 시작된 뒤에 다시 부르면 그 시점부터 시퀀스가 바뀌므로 스폰 직후 1회만 호출 전제
	UFUNCTION(BlueprintCallable, Category = "Purchase|Random")
	void InitializeRandomStream(int32 Seed);

	// =====================================================================
	// [임시] 손님 입장 확률 (백록담님 언제든지 삭제 및 변경해도 됩니다.)
	// =====================================================================
	// 가게가 open이면 스포너에 있는 가게 주위 영역포인트까지 걸어옴
	// 영역에 도착하면 CSCustomerRuntimeSubsystem::ShouldEnterStore()에서 이 값을 읽어 굴림
	// 결과에 따라 매장에 들어가거나 그대로 지나쳐 거리 반대쪽 끝까지 걸어가 사라집니다.
	// [고려해야할 점]
	// 1. 판정은 한번 뿐
	// 2. 나중에 살 생각이 없었는데 지나가다가 충동적으로 구매를 하게 하고 싶거나 할 때 영향을 받게 가능함
	// 3. 만약 어떠 손님이 편의점에서 물건을 살 목적이 있는데 이 확률에 영향을 받게되어 안사고 지나치는 오류를 범할 수도 있음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Purchase|Entry",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StoreEntryChance = 1.0f;

	// [임시] 0~1 확률을 받아 이 손님의 스트림으로 굴립니다.
	// 가게로 들어갈지 안갈지를 정할려고 확률을 굴리는 함수입니다.
	UFUNCTION(BlueprintCallable, Category = "Purchase|Random")
	bool RollChance(float Chance) const;

	// Archetype이 있으면 그 값을, 없으면 폴백값을 쓰는 헬퍼
	// 원래 private이었으나 FCSTask_BrowseShelf::Tick에서 이번 방문에 실제로 몇 개를 비교 대상에 올렸는지
	// (= 평가 예산 차감량)를 계산하려면 컴포넌트 밖에서 이 값을 읽어야 해서 public으로 옮김
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	int32 GetEffectiveTopCandidateCount() const;

	// =====================================================================
	// 거절 연출 (인지 채널)
	// =====================================================================

	// 거절 즉시표시(디버그 텍스트) 대기시간. 마법봉 시간 게이트도 이 값을 그대로 참조하므로
	// 두 곳에 따로 상수를 두지 말고 항상 이 getter를 거칠 것(Reason 하나로 두 곳이 항상 같은 값을 보게 함)
	// ShelfEmpty만 별도로 짧은 값을 씀 - 재고가 없어서 지나가는 것뿐인데 5초씩 붙잡아두면 너무 길다
	UFUNCTION(BlueprintCallable, Category = "Purchase|Rejection")
	float GetInstantDisplayDuration(ECSPurchaseRejectReason Reason) const
	{
		return Reason == ECSPurchaseRejectReason::ShelfEmpty ? EmptyShelfDisplayDuration : InstantDisplayDuration;
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

	// 거절 즉시표시(디버그 텍스트)를 몇 초간 유지할지. 마법봉 시간 게이트도 같은 값을 참조함
	// 손님마다 달라질 이유가 없는 값이라 Archetype/Fallback 이중화 대상이 아님
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	float InstantDisplayDuration = 5.0f;

	// ShelfEmpty 전용 즉시표시 대기시간. 재고가 없어서 그냥 지나가는 것뿐인데
	// 다른 거절 사유와 똑같이 5초씩 붙잡아두면 너무 길다
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	float EmptyShelfDisplayDuration = 1.5f;

	// 거절 사유별 문구/강도 구간을 담은 DataTable. 행 구조는 FCSPurchaseRejectionDisplayRow
	// 재컴파일 없이 디자이너가 문장을 조정할 수 있도록 값 전부를 여기로 뺌
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Rejection")
	TObjectPtr<UDataTable> RejectionDisplayDataTable;

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

	// Archetype 유무에 따라 실제 쓸 값을 결정하는 헬퍼들
	// Archetype이 연결되어 있으면 그 안의 값을 쓰고
	// 없으면 기본값(Fallback)을 대신 쓴다는 판단 해주는 함수들
	float GetEffectiveSelectionTemperature() const;
	float GetEffectiveMinPurchaseUtility() const;
	float GetEffectiveCountDecayLambda() const;
	float GetEffectiveRepeatCategoryPenalty() const;
	float GetEffectivePriceRejectionThreshold() const;

	// 전체 후보에 점수를 매긴 뒤 점수 내림차순으로 상위 TopCandidateCount개만 남김
	// 살아남은 후보가 하나도 없으면(전부 -1.0) OutBestRejectionInfo에 대표 거절 사유를 채움
	// CategoryBlocked가 하나라도 있으면 그게 대표, 없으면 Severity가 가장 낮은(=가장 아깝게 놓친) 것
	TArray<FCSPurchaseCandidate> ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates, FCSPurchaseRejectionInfo& OutBestRejectionInfo) const;

	// 상위 후보들 중 softmax 확률로 하나의 인덱스 뽑기
	int32 SoftmaxSelectIndex(const TArray<FCSPurchaseCandidate>& TopCandidates) const;

	// 1등 후보는 있지만 총점이 MinPurchaseUtility에 못 미쳐 거절되는 케이스 전용 헬퍼
	// ScoreCandidate는 이 경우 -1.0을 반환하지 않아(정상 0~1 점수) OutRejectionInfo가 안 채워지므로 여기서 별도로 채움
	// 총점 미달의 진짜 원인이 가격/선호 자체인지, 반복구매로 인한 카테고리 감쇠(RepeatCategoryPenalty)인지
	// 반복구매 감쇠를 걷어내도 기준선을 못 넘었을까?로 재계산해서 구분함
	// 안 그러면 이미 장바구니에 담은 물품을 똑같은 상품인데 갑자기 너무 비싸다고 함 같은 사유 오분류가 생김
	FCSPurchaseRejectionInfo BuildScoreShortfallRejectionInfo(const FCSPurchaseCandidate& Candidate, float Score) const;

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
