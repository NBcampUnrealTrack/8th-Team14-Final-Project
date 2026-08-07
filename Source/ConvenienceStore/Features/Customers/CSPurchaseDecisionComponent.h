#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "CSPurchaseDecisionComponent.generated.h"

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

	// 후보 하나에 점수를 매기는 함수
	virtual float ScoreCandidate(const FCSPurchaseCandidate& Candidate) const;

private:
	// 전체 후보에 점수를 매긴 뒤 점수 내림차순으로 상위 TopCandidateCount개만 남김
	TArray<FCSPurchaseCandidate> ScoreAndSelectTopCandidates(TArray<FCSPurchaseCandidate> Candidates) const;

	// 상위 후보들 중 softmax 확률로 하나의 인덱스 뽑기
	int32 SoftmaxSelectIndex(const TArray<FCSPurchaseCandidate>& TopCandidates) const;
};
