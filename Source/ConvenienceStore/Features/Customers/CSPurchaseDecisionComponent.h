#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/CSPurchaseDecisionTypes.h"
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
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	bool DecidePurchase(const TArray<FCSPurchaseCandidate>& Candidates, FCSPurchaseCandidate& OutSelected) const;

	// 이 손님이 어떤 종족 성향을 갖는지
	// 스폰 시점에 채워지는 것을 전제로
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Purchase|Archetype")
	TObjectPtr<UCSCustomerArchetypeDataAsset> Archetype;

	// 지금까지 담은 개수를 보고 하나 더 담을지를 0~1 확률로 계산
	UFUNCTION(BlueprintCallable, Category = "Purchase")
	float ContinueShoppingProbability(int32 ItemsInBasket) const;

protected:
	// 폴백 전용 (Archetype이 있으면 이 값들은 무시)
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	int32 FallbackTopCandidateCount = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackSelectionTemperature = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackMinPurchaseUtility = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackAcceptanceLatitude = 0.04f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackPriceElasticity = 2.62f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	int32 FallbackBudgetMedianGold = 380;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackBudgetLogSigma = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackHardWalletRatio = 1.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Fallback")
	float FallbackBudgetOverrunPenalty = 2.0f;

	// 아래 값은 Archetype(종족별 데이터)이 아니라 판단 로직 자체의 튜닝값이라 컴포넌트에 남겨둠
	// 종족마다 가중치 배분을 다르게 하고 싶어지면 그때 DA로 옮기는 걸 고려

	// 가격 점수가 전체 점수에서 차지하는 가중치
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Price = 1.0f;

	// 카테고리 점수가 전체 점수에서 차지하는 가중치
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Category = 1.0f;

	// 예산 점수가 전체 점수에서 차지하는 가중치
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Budget = 1.0f;

	// 장바구니 종료 조건 관련 파라미터
	// 담은 개수가 0(또는 1)일 때의 기본 하나 더 담을 확률
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Basket")
	float BaseContinueProbability = 0.63f;


	// 후보 하나에 점수를 매기는 함수
	virtual float ScoreCandidate(const FCSPurchaseCandidate& Candidate) const;

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

	// Archetype 유무에 따라 실제 쓸 값을 결정하는 헬퍼 3종
	// Archetype이 연결되어 있으면 그 안의 값을 쓰고
	// 없으면 기본값(Fallback)을 대신 쓴다는 판단 해주는 함수들
	int32 GetEffectiveTopCandidateCount() const;
	float GetEffectiveSelectionTemperature() const;
	float GetEffectiveMinPurchaseUtility() const;

	// 전체 후보에 점수를 매긴 뒤 점수 내림차순으로 상위 TopCandidateCount개만 남김
	TArray<FCSPurchaseCandidate> ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates) const;

	// 상위 후보들 중 softmax 확률로 하나의 인덱스 뽑기
	int32 SoftmaxSelectIndex(const TArray<FCSPurchaseCandidate>& TopCandidates) const;

	// 이 컴포넌트가 붙어있는 액터에서 CSCustomerStateComponent를 찾아옴
	UCSCustomerStateComponent* GetCustomerState() const;

	// 아직 예산이 안 뽑힌 손님이면 Archetype(또는 폴백값)을 참조해 예산을 한 번 뽑아 채움
	// 이미 초기화된 손님이면 아무것도 하지 않음
	void EnsureBudgetInitialized(UCSCustomerStateComponent& State) const;

	// 표준정규분포(평균 0, 표준편차 1) 난수 하나를 뽑음 (Box-Muller 변환)
	static float SampleStandardNormal();
};
