// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CSCustomerCounterInterface.generated.h"

// [계약] 손님 AI가 계산대에게 요구하는 최소 기능.
// 구현체:
//   - Demo/BMS/CSDemoMockCounter 의 테스트용 계산대
//   -  Features/Checkout 의 실제 계산대 ← 플레이어 파트가 구현
// 계산 완료 처리는 계산대가 아니라 UCSStoreCustomerSubsystem::CompleteFrontCheckout() 호출로 한다.

UINTERFACE(MinimalAPI)
class UCSCustomerCounterInterface : public UInterface
{
	GENERATED_BODY()
};


class CONVENIENCESTORE_API ICSCustomerCounterInterface
{
	GENERATED_BODY()

public:
	// 대기열 Index번째 손님이 설 월드 위치
	virtual FVector GetQueueSlotLocation(const int32 Index) const = 0;
	/*
	 * 손님 AI가 계산대에 대해 알아야 할 건 딱 이거 하나뿐이기 때문.
	 * "내가 몇 번째 줄에 서면 어디 서 있어야 하는지"만 알면 WaitInQueue 태스크가 그 좌표로 이동시킬 수 있음.
	 * 계산 로직, 결제 처리, 상품 스캔 같은 건 손님이 알 필요가 없어서(그건 플레이어/편의점 파트 영역) 인터페이스에 넣지 않음
	 */

	// 아래 세 함수는 기본 구현을 가진 가상 함수
	// 새 계산대 구현체가 생겨도 컴파일이 깨지지 않고, 기존 동작(무제한 대기열 + 도착 판정 없음)을 그대로 유지합니다.

	// Index번째 슬롯의 도착 판정 영역 안에 이 좌표가 있는지 확인합니다.
	// 영역 개념이 없는 계산대는 항상 true를 반환해 예전처럼 "줄에 등록되면 곧 계산 가능"으로 동작합니다.
	virtual bool IsPointInsideQueueSlotZone(const int32 Index, const FVector& WorldPoint) const
	{
		return true;
	}

	// 이 계산대에 실제로 설 수 있는 사람 수
	// 정원 개념이 없는 계산대는 0을 반환해 기존의 무제한 동작을 유지합니다.
	virtual int32 GetEffectiveMaxQueueLength() const
	{
		return 0;
	}

	// Index번째 슬롯에 선 손님이 바라볼 방향
	// 방향을 지정하지 않는 계산대는 false를 반환해 손님이 걸어온 방향 그대로 서 있게 놔둠
	virtual bool TryGetQueueSlotFacing(const int32 Index, FRotator& OutFacing) const
	{
		OutFacing = FRotator::ZeroRotator;
		return false;
	}

	// Index번째 슬롯의 도착 판정 영역을 한 번에 돌려줍니다.
	// 손님 AI는 이 중심으로 이동하고 이 반경 안에서 멈춰야 합니다.
	// 이동 목표와 도착 판정이 같은 원을 봐야 "이동은 끝났는데 판정은 안 되는" 구간이 생기지 않습니다.
	// 중심은 NavMesh 바닥 높이로 내려온 값이어야 합니다.
	// 영역 개념이 없는 계산대는 false를 반환합니다. 그러면 호출부가 예전 동작으로 돌아갑니다.
	virtual bool TryGetQueueSlotZone(const int32 Index, FVector& OutCenter, float& OutRadius) const
	{
		OutCenter = GetQueueSlotLocation(Index);
		OutRadius = 0.0f;
		return false;
	}

	// 확정된 인원 다음의 빈 슬롯을 기준으로 집결 지점의 중심과 반경을 조회
	// 영역을 지원하지 않으면 false를 반환해 호출부에서 꼬리 슬롯 좌표를 사용
	virtual bool TryGetQueueEntryZone(const int32 CommittedCount, FVector& OutCenter, float& OutRadius) const
	{
		OutCenter = GetQueueSlotLocation(CommittedCount);
		OutRadius = 0.0f;
		return false;
	}

	// 지정한 좌표가 집결 지점 영역에 도착해 순번을 확정할 수 있는지 판정
	// 영역을 지원하지 않으면 항상 통과시켜 도착 대기 없이 순번 확정을 허용
	virtual bool IsPointInsideQueueEntryZone(const int32 CommittedCount, const FVector& WorldPoint) const
	{
		return true;
	}

	// 손님 AI가 슬롯으로 이동할 때 멈춰도 되는 반경입니다.
	// TryGetQueueSlotZone이 돌려주는 도착 판정 반경보다 작거나 같아야 합니다.
	// 크면 "이동은 완료인데 도착 판정은 안 되는" 구간이 생겨 손님이 그 자리에 영구히 멈춥니다.
	// 계산대마다 대기줄 간격과 판정 반경이 다르므로 값은 계산대가 자기 데이터에서 유도합니다.
	// 0 이하를 반환하면 호출부가 예전 하드코딩 값으로 폴백합니다.
	virtual float GetQueueSlotStopRadius() const
	{
		return 0.0f;
	}
};
