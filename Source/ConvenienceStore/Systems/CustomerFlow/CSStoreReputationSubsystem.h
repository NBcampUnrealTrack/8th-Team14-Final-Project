// 계산 포기 + 빈손 퇴장 + 가격 불쾌 + 쓰레기 목격을 하루 동안 집계했다가 마감 시점에 평판 점수로 반영하는 서브시스템
// 정상 결제는 카운트만 하고, 계산 포기/빈손 퇴장/가격 불쾌는 각각 확률 추첨을 거쳐서만 카운트한다
// 쓰레기 목격은 방송 전에 이미 CSCustomerRuntimeSubsystem에서 확률 추첨(하루 1회 캡 + 목격 확률)을 거치므로 여기서는 그대로 누적만 한다
// 손님 경험이 나쁜 날마다 원인 전부가 매번 깎이면 하루 변동폭이 지나치게 커지기 때문에 이 추첨이 완충 역할을 한다

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Math/RandomStream.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "CSStoreReputationSubsystem.generated.h"

class ACSCustomerCharacter;

// 하루 카운터(입장/정상결제/계산포기/빈손퇴장/가격불쾌/쓰레기목격) 중 하나가 바뀔 때마다 방송
// 실시간 정산창(C)이 아직 마감 전인 오늘자 평판 변동을 미리 보여줄 때 구독해서 씀
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnReputationStatsChanged);

UCLASS()
class CONVENIENCESTORE_API UCSStoreReputationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 월드가 시작될 때 CSCustomerRuntimeSubsystem / CSStoreGameState의 방송을 구독
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// 월드가 끝날 때(레벨 전환, PIE 종료 등) 구독을 정리
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "Reputation")
	FCSOnReputationStatsChanged OnReputationStatsChanged;

	// 아직 마감(SettleDailyReputation) 전, 지금까지 쌓인 하루 카운터만으로 평판 델타를 미리 계산함
	// AddReputation을 호출하지 않으므로 점수는 바뀌지 않음 - 실시간 정산창(C) 조회 전용
	int32 GetProjectedReputationDelta() const;

protected:
	// 만족도 계산에서 계산 포기 1건이 정상 결제 몇 건과 같은 무게로 깎이는지
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float CheckoutAbandonedWeight = 2.5f;

	// 만족도 계산에서 빈손 퇴장 1건이 정상 결제 몇 건과 같은 무게로 깎이는지
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float EmptyHandedWeight = 0.5f;

	// 이 만족도를 기준선으로 삼아 위/아래로 평판 상승/하락이 갈린다
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float SatisfactionBaseline = 0.775f;

	// 만족도 1단위 차이를 평판 점수 몇 점으로 환산할지
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float ReputationDeltaScale = 1500.0f;

	// 하루 평판 변화폭 상하한(절대값)
	UPROPERTY(EditAnywhere, Category = "Reputation", meta = (ClampMin = "0.0"))
	float MaxDailyReputationDelta = 600.0f;

	// 계산 포기 / 빈손 퇴장을 실제 카운트에 반영할 확률
	UPROPERTY(EditAnywhere, Category = "Reputation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CountRollChance = 0.3f;

	// 가격 불쾌 3단 계단식 곡선 - Severity가 이 값 미만이면 가중치 0(무시)
	UPROPERTY(EditAnywhere, Category = "Reputation|PriceUpset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PriceUpsetLowThreshold = 0.4f;

	// 이 값부터 위 구간(HighWeight 쪽)으로 넘어간다
	UPROPERTY(EditAnywhere, Category = "Reputation|PriceUpset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PriceUpsetMidThreshold = 0.6f;

	// MidThreshold부터 이 폭만큼 지나면(=0.6+0.9 지점) 가중치가 최대(1.0)로 포화
	UPROPERTY(EditAnywhere, Category = "Reputation|PriceUpset", meta = (ClampMin = "0.01"))
	float PriceUpsetHighSpan = 0.3f;

	// LowThreshold~MidThreshold 구간에서 도달하는 가중치(동시에 위 구간의 시작값)
	UPROPERTY(EditAnywhere, Category = "Reputation|PriceUpset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PriceUpsetMidWeight = 0.3f;

	// MidThreshold 이후 구간에서 추가로 더해지는 가중치 몫(MidWeight + 이 값 = 최대 1.0)
	UPROPERTY(EditAnywhere, Category = "Reputation|PriceUpset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PriceUpsetHighWeight = 0.7f;

	// 만족도 계산에서 가격 불쾌 누적치(DailyPriceUpsetSum) 1단위가 정상 결제 몇 건과 같은 무게로 깎이는지
	UPROPERTY(EditAnywhere, Category = "Reputation|PriceUpset")
	float PriceUpsetWeight = 1.5f;

	// 만족도 계산에서 쓰레기 목격 1건이 정상 결제 몇 건과 같은 무게로 깎이는지
	// 배선만 해둔 초기값 - 실제 튜닝은 밸런싱 단계에서 진행
	UPROPERTY(EditAnywhere, Category = "Reputation|Trash")
	float TrashWitnessWeight = 0.5f;

private:
	// 정상 결제 카운트만 올림(추첨 없음)
	UFUNCTION()
	void HandleCheckoutCompleted(ACSCustomerCharacter* Customer);

	// 전용 스트림으로 추첨 후 카운트
	UFUNCTION()
	void HandleCheckoutAbandoned(ACSCustomerCharacter* Customer, float WaitedSeconds);

	// 진짜 빈손 퇴장만 골라 추첨
	UFUNCTION()
	void HandleCustomerDespawned(ACSCustomerCharacter* Customer, bool bCheckoutDone);

	// 가격 불쾌 반응(거절이든 반응 후 구매든)을 사유 필터링 + 1일 1회 제한 + 추첨을 거쳐 누적
	UFUNCTION()
	void HandlePurchaseRejected(ACSCustomerCharacter* Customer, FCSPurchaseRejectionInfo Info);

	// 쓰레기 목격을 누적. 하루 1회 캡과 30% 목격 확률은 방송 전에 CSCustomerRuntimeSubsystem이
	// 이미 거쳤으므로, 여기서는 HandlePurchaseRejected와 달리 RollShouldCount()를 다시 거치지 않는다
	UFUNCTION()
	void HandleTrashWitnessed(ACSCustomerCharacter* Customer);

	// 마감 종 확정 직후, 정산 위젯이 뜨기 전에 동기 호출된다(RequestDailySummary 안에서 브로드캐스트)
	UFUNCTION()
	void HandleDailySummaryRequested();

	// 하루 카운터 초기화
	UFUNCTION()
	void HandleBusinessDayStarted(int32 NewBusinessDay);

	// 누적 카운터로 만족도와 평판 델타를 계산해 AddReputation을 호출
	void SettleDailyReputation() const;

	// SettleDailyReputation과 GetProjectedReputationDelta가 공유하는 만족도 계산식
	// (지금까지 쌓인 카운터 기준. DailyEnteredCustomers==0이면 호출하지 않아야 함)
	float ComputeSatisfaction() const;

	// ComputeSatisfaction 결과를 스케일·클램프해서 평판 델타(정수)로 환산
	int32 ComputeReputationDelta() const;

	// 하루 카운터 5개(입장/정상결제/계산포기/빈손퇴장/가격불쾌)를 0으로 되돌림
	void ResetDailyCounters();

	// 계산 포기 / 빈손 퇴장 / 가격 불쾌를 카운트에 반영할지 CountRollChance로 추첨
	bool RollShouldCount() const;

	// 가격 불쾌 Severity(0~1)를 3단 계단식으로 평판 가중치(0~1)로 환산
	float ComputePriceUpsetWeight(float Severity) const;

	// 하루 동안 매장에 실제로 들어온 손님 수 (가게 앞만 지나간 행인 / 영업종료 강제 퇴장 손님은 제외)
	int32 DailyEnteredCustomers = 0;

	// 하루 동안 정상 결제를 완료한 횟수
	int32 DailyNormalCheckouts = 0;

	// 하루 동안 추첨을 통과해 카운트에 반영된 계산 포기 횟수
	int32 DailyCheckoutAbandoned = 0;

	// 하루 동안 추첨을 통과해 카운트에 반영된 빈손 퇴장 횟수
	int32 DailyEmptyHandedDepartures = 0;

	// 하루 동안 누적된 가격 불쾌 가중치 합(손님당 최대 1.0씩, 1일 1회 제한 적용됨)
	float DailyPriceUpsetSum = 0.0f;

	// 하루 동안 다른 손님의 쓰레기를 목격해 평판에 반영된 횟수(손님당 1일 1회 제한 적용됨)
	// 목격 판정 자체(하루 1회 캡 + 30% 확률)는 CSCustomerRuntimeSubsystem이 이미 마쳤으므로
	// 가격 불쾌와 달리 여기서는 별도 추첨 없이 그대로 누적한다
	int32 DailyTrashWitnessed = 0;

	// 계산 포기 / 빈손 퇴장 / 가격 불쾌 추첨 전용 스트림
	mutable FRandomStream ReputationRollStream;
};
