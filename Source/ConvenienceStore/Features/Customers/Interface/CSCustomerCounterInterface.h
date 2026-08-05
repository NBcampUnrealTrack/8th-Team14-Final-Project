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
};
