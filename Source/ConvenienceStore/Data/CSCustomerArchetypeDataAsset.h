#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/CSProductTypes.h"
#include "Data/CSVillainTypes.h"
#include "CSCustomerArchetypeDataAsset.generated.h"

class UCSCustomerVisualDataAsset;

// 종족(연령대)별 소비 성향을 담는 성격 카드
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSCustomerArchetypeDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// 종족 이름
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Identity")
	FText DisplayName;

	// [한길] 종족별 신체 크기입니다. 현재 공통 손님 크기가 4.0이므로 여기서는 1을 기본값으로 사용합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Physical",
		meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CharacterScale = 1.0f;

	// 스폰 - 값은 여기 두고 실제 손님 생성은 동선 담당 파트가 읽어 쓰기 - 백무송님
	// 손님 100명 중 이 종족이 몇 명인가 -> 6종족 합계가 1.0이 되게 넣음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Spawn",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpawnWeightBase = 0.2f;

	// 지갑 -------
	// 예산의 중간값 (평균이 아님)
	// 손님 절반은 이보다 적게, 절반은 많게 나옴
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Budget",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 BudgetMedianGold = 380;

	// 예산이 얼마나 흩어질지를 정하는 편차 다이얼 -------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Budget",
		meta = (ClampMin = "0.05", ClampMax = "1.5"))
	float BudgetLogSigma = 0.6f;

	// 쓸 생각한 돈(소프트 예산) 대비 물리적으로 낼 수 있는 돈(하드 지갑)의 배수
	// 하드 지갑 = 소프트 예산 × HardWalletRatio
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Budget",
		meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float HardWalletRatio = 1.6f;

	// 소프트 예산을 넘어선 구간에서 구매 의욕이 줄어드는 속도 -----------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Budget",
		meta = (ClampMin = "0.1", ClampMax = "6.0"))
	float BudgetOverrunPenalty = 2.0f;

	// 가격 성향 -------

	// 가격이 1% 오르면 판매가 몇 % 줄어드는가 - 클수록 가격에 예민
	// 10대와 60대가 가장 예민하고 30~40대가 가장 둔감(164개국 실측)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Price",
		meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float PriceElasticity = 2.62f;

	// 가격이 이 비율 안쪽에서 움직이면 손님이 눈치채지 못함 -----
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Price",
		meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float AcceptanceLatitude = 0.04f;

	// 구매 성향 -----

	// 충동성 -----
	// 나이가 어릴수록 높게 배정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Behavior",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Impulsiveness = 0.5f;

	// 카테고리 성향 -------

	// 카테고리별 기본 선호도
	// 0.5 = 전체 평균, 그보다 높으면 더 좋아함 / 낮으면 덜 좋아함
	// 맵에 없는 카테고리는 0.5(평균)로 취급
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Category")
	TMap<ECSProductCategory, float> CategoryPreference;

	// 이 종족이 절대 사지 않는 카테고리
	// 여기 포함되면 점수와 무관하게 무조건 미구매
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Category")
	TSet<ECSProductCategory> BlockedCategories;

	// 같은 카테고리를 이미 담은 상태에서 또 같은 카테고리를 볼 때 점수를 깎는 배수 (한계효용 체감)
	// 반복 횟수만큼 pow(RepeatCategoryPenalty, 반복횟수)로 곱해서 감쇠시킴
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Category",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RepeatCategoryPenalty = 0.7f;

	// 탐색 ---------

	// 매장에 머무는 시간(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Browsing",
		meta = (ClampMin = "10.0"))
	float DwellTimeSeconds = 180.0f;

	// 나가기 전까지 살펴볼 수 있는 상품 개수 ------------
	// 체류시간에서 비례로 계산
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Browsing",
		meta = (ClampMin = "1"))
	int32 EvaluationBudget = 8;

	// 이 종족의 목표 평균 구매 개수 ------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Browsing",
		meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float TargetBasketSize = 2.7f;

	// 담을수록 하나 더 담을 확률이 지수적으로 줄어드는 감쇠 계수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Browsing",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CountDecayLambda = 0.15f;

	// 선택 ------------

	// 1등 점수가 이 값보다 낮으면 아무것도 사지 않음
	// 초기 목표는 빈손 퇴장 10~15%
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Selection",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinPurchaseUtility = 0.35f;

	// 후보 중에서 뽑을 때 얼마나 랜덤하게 할지를 정하는 온도 다이얼 -------
	// 최고점만 뽑지 않는다는 기획 확정 원칙이라 0으로 두면 안 됨
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Selection",
		meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float SelectionTemperature = 0.25f;

	// 점수 상위 몇 개까지를 후보로 볼 것인가 --------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Selection",
		meta = (ClampMin = "1", ClampMax = "10"))
	int32 TopCandidateCount = 3;

	// 계산이 필요한 함수 ---------

	// 이 손님이 실제로 뽑은 소프트 예산에 대응하는 하드 지갑 한도를 돌려줌
	// 중간값(BudgetMedianGold)이 아니라 그 손님이 뽑은 값을 넘겨야 함
	UFUNCTION(BlueprintPure, Category = "Archetype|Budget")
	int32 GetHardWalletLimitGold(int32 SoftBudgetGold) const;

	// 빌런(백록담님 언제든 수정 가능합니다) -------

	// 스폰된 손님이 빌런일 확률.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Villain",
	meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VillainChance = 0.0f;

	// 빌런이 무슨 짓을 할지의 가중치 목록.
	// (예를들어 10대는 훔치는것보다 주류를 더 훔칠려고 한다 등등)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Villain")
	TArray<FCSVillainActionWeight> VillainActionWeights;


	// 외형 -------

	// 이 종족의 메시/애니메이션 정보
	// 외형 관련 데이터는 전부 UCSCustomerVisualDataAsset 쪽에서 관리하고 여기서는 참조만 함
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Archetype|Visual")
	TObjectPtr<UCSCustomerVisualDataAsset> Visual;
};
