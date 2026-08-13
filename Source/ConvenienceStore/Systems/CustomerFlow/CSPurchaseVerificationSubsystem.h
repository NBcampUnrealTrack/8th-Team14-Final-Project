// 구매 판단 로직이 실제로 목표 수치에 맞게 작동하는지
// 계산 완료 시점마다 자동으로 집계해서 로그로 보여주는 검증용 서브시스템
// 목표치(로드맵 기준): 평균 장바구니 2.5~3.0개, 1개만 사는 비율 37~50%

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSPurchaseVerificationSubsystem.generated.h"

class ACSCustomerCharacter;

UCLASS()
class CONVENIENCESTORE_API UCSPurchaseVerificationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 월드가 시작될 때 CSCustomerRuntimeSubsystem의 방송을 구독
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// 월드가 끝날 때(레벨 전환, PIE 종료 등) 구독을 정리
	virtual void Deinitialize() override;

protected:
	// 몇 명이 계산을 마칠 때마다 요약 로그를 찍을지
	// 매번 찍으면 개별 로그와 섞여 알아보기 어려워서 일정 간격으로만 찍음
	UPROPERTY(EditAnywhere, Category = "Verification", meta = (ClampMin = "1"))
	int32 LogIntervalCount = 5;

private:
	// CSCustomerRuntimeSubsystem::OnCheckoutCompleted를 구독하는 핸들러
	UFUNCTION()
	void HandleCheckoutCompleted(ACSCustomerCharacter* Customer);

	// CSCustomerRuntimeSubsystem::OnCustomerDespawned를 구독하는 핸들러
	UFUNCTION()
	void HandleCustomerDespawned(ACSCustomerCharacter* Customer, bool bCheckoutDone);

	// 누적 통계를 바탕으로 평균 장바구니 개수 / 1개만 산 비율을 계산해 로그로 출력합
	void LogSummary() const;

	// 지금까지 계산을 마친 손님 수
	int32 TotalCheckouts = 0;

	// 지금까지 팔린 총 상품 개수 (모든 손님의 장바구니 개수를 합산)
	int32 TotalItemsPurchased = 0;

	// 1개만 사고 간 손님 수
	int32 SingleItemCheckouts = 0;

	// 매장을 완전히 떠난 손님 총원 (구매 여부 무관)
	int32 TotalCustomersLeft = 0;

	// 그중 결제를 한 번도 완료하지 못하고 빈손으로 나간 손님 수
	int32 EmptyHandedDepartures = 0;

	// 결제는 못 했지만 계산 포기(대기열 시간 초과)로 나간 손님 수
	int32 CheckoutAbandonedDepartures = 0;
};
