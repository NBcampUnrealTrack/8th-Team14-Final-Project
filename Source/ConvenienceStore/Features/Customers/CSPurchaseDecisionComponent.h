#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "CSPurchaseDecisionComponent.generated.h"

struct FCSProductDataRow;

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

protected:
	// ---- 선택(softmax) 관련 파라미터 ----

	// 점수 계산 후 상위 몇 개까지만 구매 후보로 남길지
	// 값이 클수록 손님이 덜 인기 있는 물건도 종종 고르게 됨
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Selection")
	int32 TopCandidateCount = 3;

	// softmax 선택의 온도(T)
	// 작을수록 1등 후보로 확률이 쏠리고 클수록 후보들 사이 확률 차이가 흐려짐
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Selection")
	float SelectionTemperature = 0.25f;

	// 1등 후보의 점수가 이 값보다 낮으면 아무것도 사지 않음
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Selection")
	float MinPurchaseUtility = 0.35f;

	// ---- 가격 효용 점수 관련 파라미터 ----

	// 준거가격 대비 이 비율 이내는 손님이 차이를 못 느낀 것으로 취급 (무감각 구간)
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Price")
	float PriceDeadZoneRatio = 0.04f;

	// 데드존을 벗어난 뒤 점수가 얼마나 가파르게 떨어지는지
	// 값이 클수록 조금만 벗어나도 점수가 빠르게 0에 가까워짐
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Price")
	float PriceSteepness = 8.0f;

	// 가격 점수가 전체 점수에서 차지하는 가중치
	UPROPERTY(EditDefaultsOnly, Category = "Purchase|Weights")
	float W_Price = 1.0f;

	// 후보 하나에 점수를 매기는 함수
	virtual float ScoreCandidate(const FCSPurchaseCandidate& Candidate) const;

	// 실제 판매가와 준거가격을 비교해 0~1 가격 효용 점수를 계산
	// 데드존 안이면 1.0, 벗어나면 대칭 시그모이드로 감점
	float ComputePriceScore(float ActualPrice, float ReferencePrice) const;

private:
	// 후보가 가리키는 상품 마스터 데이터를 데이터테이블에서 조회합니다.
	const FCSProductDataRow* GetProductRow(const FCSPurchaseCandidate& Candidate) const;

	// 전체 후보에 점수를 매긴 뒤 점수 내림차순으로 상위 TopCandidateCount개만 남김
	TArray<FCSPurchaseCandidate> ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates) const;

	// 상위 후보들 중 softmax 확률로 하나의 인덱스 뽑기
	int32 SoftmaxSelectIndex(const TArray<FCSPurchaseCandidate>& TopCandidates) const;
};
