// 계산 포기 + 빈손 퇴장을 하루 동안 집계했다가 마감 시점에 평판 점수로 반영하는 서브시스템
// 정상 결제는 카운트만 하고, 계산 포기/빈손 퇴장은 각각 확률 추첨을 거쳐서만 카운트한다
// 손님 경험이 나쁜 날마다 원인 전부가 매번 깎이면 하루 변동폭이 지나치게 커지기 때문에 이 추첨이 완충 역할을 한다

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Math/RandomStream.h"
#include "CSStoreReputationSubsystem.generated.h"

class ACSCustomerCharacter;

UCLASS()
class CONVENIENCESTORE_API UCSStoreReputationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 월드가 시작될 때 CSCustomerRuntimeSubsystem / CSStoreGameState의 방송을 구독
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// 월드가 끝날 때(레벨 전환, PIE 종료 등) 구독을 정리
	virtual void Deinitialize() override;

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

	// 마감 종 확정 직후, 정산 위젯이 뜨기 전에 동기 호출된다(RequestDailySummary 안에서 브로드캐스트)
	UFUNCTION()
	void HandleDailySummaryRequested();

	// 하루 카운터 초기화
	UFUNCTION()
	void HandleBusinessDayStarted(int32 NewBusinessDay);

	// 누적 카운터로 만족도와 평판 델타를 계산해 AddReputation을 호출
	void SettleDailyReputation() const;

	// 하루 카운터 4개(입장/정상결제/계산포기/빈손퇴장)를 0으로 되돌림
	void ResetDailyCounters();

	// 계산 포기 / 빈손 퇴장을 카운트에 반영할지 CountRollChance로 추첨
	bool RollShouldCount() const;

	// 하루 동안 매장에 실제로 들어온 손님 수 (가게 앞만 지나간 행인은 제외)
	int32 DailyEnteredCustomers = 0;

	// 하루 동안 정상 결제를 완료한 횟수
	int32 DailyNormalCheckouts = 0;

	// 하루 동안 추첨을 통과해 카운트에 반영된 계산 포기 횟수
	int32 DailyCheckoutAbandoned = 0;

	// 하루 동안 추첨을 통과해 카운트에 반영된 빈손 퇴장 횟수
	int32 DailyEmptyHandedDepartures = 0;

	// 계산 포기 / 빈손 퇴장 추첨 전용 스트림
	mutable FRandomStream ReputationRollStream;
};
