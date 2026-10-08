// 계산 포기 + 빈손 퇴장 + 가격 불쾌 + 쓰레기 목격을 하루 동안 집계했다가 마감 시점에 평판 점수로 반영하는 서브시스템
// 추첨 없이 손님 한 명당 항목별로 한 번씩 고정 가중치만큼 깎는다. 운에 따라 하루 결과가 출렁이면
// 플레이어가 무엇 때문에 깎였는지 읽을 수 없고, 실시간 정산창(C) 예상치와 실제 정산도 어긋난다

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CSPurchaseDecisionTypes.h"
#include "Systems/SaveLoad/CSSaveParticipant.h"
#include "CSStoreReputationSubsystem.generated.h"

class ACSCustomerCharacter;
class UCSSaveGame;

// 하루 카운터(입장/정상결제/계산포기/빈손퇴장/가격불쾌/쓰레기목격) 중 하나가 바뀔 때마다 방송
// 실시간 정산창(C)이 아직 마감 전인 오늘자 평판 변동을 미리 보여줄 때 구독해서 씀
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnReputationStatsChanged);

UCLASS()
class CONVENIENCESTORE_API UCSStoreReputationSubsystem : public UWorldSubsystem,
	public ICSSaveParticipant
{
	GENERATED_BODY()

public:
	// 월드가 시작될 때 CSCustomerRuntimeSubsystem / CSStoreGameState의 방송을 구독
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// 월드가 끝날 때(레벨 전환, PIE 종료 등) 구독을 정리
	virtual void Deinitialize() override;

	// ICSSaveParticipant
	// 영업 종료 직후 저장한 세이브를 불러온 뒤 잠자기 정산이 같은 결과를 내도록 하루 카운터를 기록·복원
	virtual void CaptureSaveData(UCSSaveGame& OutSave) const override;
	virtual void RestoreSaveData(const UCSSaveGame& InSave) override;

	UPROPERTY(BlueprintAssignable, Category = "Reputation")
	FCSOnReputationStatsChanged OnReputationStatsChanged;

	// 아직 마감(SettleDailyReputation) 전, 지금까지 쌓인 하루 카운터만으로 평판 델타를 미리 계산함
	// AddReputation을 호출하지 않으므로 점수는 바뀌지 않음 - 실시간 정산창(C) 조회 전용
	int32 GetProjectedReputationDelta() const;

protected:
	// 만족도 계산에서 계산 포기 1건이 정상 결제 몇 건과 같은 무게로 깎이는지
	// 결제를 못 했으니 정상 결제 1건은 이미 빠져 있고, 이 값은 그 위에 더 깎는 몫이다
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float CheckoutAbandonedWeight = 0.75f;

	// 만족도 계산에서 빈손 퇴장 1건이 정상 결제 몇 건과 같은 무게로 더 깎이는지
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float EmptyHandedWeight = 0.15f;

	// 이 만족도를 기준선으로 삼아 위/아래로 평판 상승/하락이 갈린다
	// 계산을 한두 명 놓친 무난한 날(약 0.93)이 +250 안팎이 되게 맞췄다
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float SatisfactionBaseline = 0.83f;

	// 기준선보다 높을 때 만족도 1단위를 평판 몇 점으로 환산할지
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float ReputationGainScale = 2500.0f;

	// 기준선보다 낮을 때 만족도 1단위를 평판 몇 점으로 환산할지
	// 오를 때보다 작게 둬서 실수한 하루를 잘한 하루로 거의 되찾을 수 있게 한다
	UPROPERTY(EditAnywhere, Category = "Reputation")
	float ReputationLossScale = 1000.0f;

	// 하루 평판 상승 상한
	UPROPERTY(EditAnywhere, Category = "Reputation", meta = (ClampMin = "0.0"))
	float MaxDailyReputationGain = 400.0f;

	// 하루 평판 하락 상한(절대값). 시작 등급에서 한 등급 내려가려면 최악의 날이 4일 이어져야 한다
	UPROPERTY(EditAnywhere, Category = "Reputation", meta = (ClampMin = "0.0"))
	float MaxDailyReputationLoss = 300.0f;

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
	// 너무 비싸면 이미 안 사고 나가서 매출과 빈손 퇴장으로도 깎이므로 크게 두지 않는다
	UPROPERTY(EditAnywhere, Category = "Reputation|PriceUpset")
	float PriceUpsetWeight = 0.5f;

	// 만족도 계산에서 쓰레기 목격 1건이 정상 결제 몇 건과 같은 무게로 깎이는지
	// 목격 확률이 30%라 하루 종일 방치해도 손님 셋 중 하나 꼴로만 잡힌다
	UPROPERTY(EditAnywhere, Category = "Reputation|Trash")
	float TrashWitnessWeight = 1.0f;

private:
	UFUNCTION()
	void HandleCheckoutCompleted(ACSCustomerCharacter* Customer);

	UFUNCTION()
	void HandleCheckoutAbandoned(ACSCustomerCharacter* Customer, float WaitedSeconds);

	// 입장 수, 빈손 퇴장, 가격 불쾌, 쓰레기 목격을 여기서 한꺼번에 센다
	// 목격과 가격 불쾌를 일어난 순간에 세면 마감 강제 퇴장 손님 몫이 분모에서만 빠져 비율이 부푼다
	UFUNCTION()
	void HandleCustomerDespawned(ACSCustomerCharacter* Customer, bool bCheckoutDone);

	// 가격 불쾌 강도 중 가장 큰 값만 손님에게 기억해 둔다
	UFUNCTION()
	void HandlePurchaseRejected(ACSCustomerCharacter* Customer, FCSPurchaseRejectionInfo Info);

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

	// 가격 불쾌 Severity(0~1)를 3단 계단식으로 평판 가중치(0~1)로 환산
	float ComputePriceUpsetWeight(float Severity) const;

	// 하루 동안 매장에 실제로 들어온 손님 수 (가게 앞만 지나간 행인 / 영업종료 강제 퇴장 손님은 제외)
	int32 DailyEnteredCustomers = 0;

	// 하루 동안 정상 결제를 완료한 횟수
	int32 DailyNormalCheckouts = 0;

	int32 DailyCheckoutAbandoned = 0;

	int32 DailyEmptyHandedDepartures = 0;

	// 손님마다 가장 큰 가격 불쾌 가중치(최대 1.0)를 하나씩 더한 합
	float DailyPriceUpsetSum = 0.0f;

	// 다른 손님의 쓰레기를 목격한 손님 수
	int32 DailyTrashWitnessed = 0;
};
