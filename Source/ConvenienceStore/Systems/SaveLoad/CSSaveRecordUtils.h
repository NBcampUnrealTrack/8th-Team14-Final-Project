#pragma once

#include "CoreMinimal.h"
#include "Data/CSSaveTypes.h"
#include "Data/CSDeliveryOrderTypes.h"
#include "Data/CSJournalTypes.h"
#include "Data/CSProductTypes.h"

// 런타임 구조체와 세이브 레코드 사이의 공용 변환 함수 모음
// 여러 참가자가 같은 구조체를 다루므로 변환 코드 중복을 방지
namespace CSSaveRecordUtils
{
	// ----- 배달 주문 -----

	// 배달 주문 런타임 데이터를 레코드로 변환
	inline FCSDeliveryOrderRecord ToRecord(const FCSDeliveryOrderRuntime& InOrder)
	{
		// 주문 번호·상태·수신처·보상·마감 시각을 그대로 복사
		FCSDeliveryOrderRecord Record;
		Record.OrderId = InOrder.OrderId;
		Record.Status = InOrder.Status;
		Record.RecipientId = InOrder.RecipientId;
		Record.RecipientDisplayName = InOrder.RecipientDisplayName;
		Record.RecipientRegionName = InOrder.RecipientRegionName;
		Record.RewardGold = InOrder.RewardGold;
		Record.ReputationReward = InOrder.ReputationReward;
		Record.AcceptanceDeadlineMinutes = InOrder.AcceptanceDeadlineMinutes;
		Record.DeliveryDeadlineMinutes = InOrder.DeliveryDeadlineMinutes;

		// 상품 행 핸들을 행 이름과 테이블 경로로 이루어진 세이브 행 참조로 변환
		// 테이블 포인터는 레벨 재오픈 후 유효하지 않으므로 경로로 기록
		Record.RequiredProducts.Reserve(InOrder.RequiredProducts.Num());
		for (const FCSDeliveryOrderLine& Line : InOrder.RequiredProducts)
		{
			FCSDeliveryOrderLineRecord LineRecord;
			LineRecord.ProductRow = FCSSaveRowRef::FromHandle(Line.ProductRow);
			LineRecord.Quantity = Line.Quantity;
			Record.RequiredProducts.Add(LineRecord);
		}

		return Record;
	}

	// 배달 주문 레코드를 런타임 데이터로 변환
	inline FCSDeliveryOrderRuntime ToRuntime(
		const FCSDeliveryOrderRecord& InRecord,
		const UDataTable* FallbackProductTable = nullptr)
	{
		// 주문 번호·상태·수신처·보상·마감 시각을 그대로 복사
		FCSDeliveryOrderRuntime Order;
		Order.OrderId = InRecord.OrderId;
		Order.Status = InRecord.Status;
		Order.RecipientId = InRecord.RecipientId;
		Order.RecipientDisplayName = InRecord.RecipientDisplayName;
		Order.RecipientRegionName = InRecord.RecipientRegionName;
		Order.RewardGold = InRecord.RewardGold;
		Order.ReputationReward = InRecord.ReputationReward;
		Order.AcceptanceDeadlineMinutes = InRecord.AcceptanceDeadlineMinutes;
		Order.DeliveryDeadlineMinutes = InRecord.DeliveryDeadlineMinutes;

		// 세이브 행 참조를 런타임 행 핸들로 변환
		// 테이블 경로를 해석하지 못하면 FallbackProductTable을 사용하고 둘 다 없으면 빈 핸들로 추가
		Order.RequiredProducts.Reserve(InRecord.RequiredProducts.Num());
		for (const FCSDeliveryOrderLineRecord& LineRecord : InRecord.RequiredProducts)
		{
			FCSDeliveryOrderLine Line;
			Line.ProductRow = LineRecord.ProductRow.ToHandle(FallbackProductTable);
			Line.Quantity = LineRecord.Quantity;
			Order.RequiredProducts.Add(Line);
		}

		return Order;
	}

	// ----- 수첩 -----

	// 수첩 배달 기록을 레코드로 변환
	inline FCSJournalDeliveryRecord ToRecord(const FCSJournalDelivery& InDelivery)
	{
		FCSJournalDeliveryRecord Record;
		Record.RecordId = InDelivery.RecordId;
		Record.LevelId = InDelivery.LevelId;
		Record.AcceptedDay = InDelivery.AcceptedDay;
		Record.FinishedDay = InDelivery.FinishedDay;

		// 기록에 포함된 주문은 배달 주문 변환 함수로 변환
		Record.Order = ToRecord(InDelivery.Order);
		return Record;
	}

	// 수첩 배달 기록 레코드를 런타임 데이터로 변환
	inline FCSJournalDelivery ToRuntime(
		const FCSJournalDeliveryRecord& InRecord,
		const UDataTable* FallbackProductTable = nullptr)
	{
		FCSJournalDelivery Delivery;
		Delivery.RecordId = InRecord.RecordId;
		Delivery.LevelId = InRecord.LevelId;
		Delivery.AcceptedDay = InRecord.AcceptedDay;
		Delivery.FinishedDay = InRecord.FinishedDay;

		// 기록에 포함된 주문은 배달 주문 변환 함수로 변환
		Delivery.Order = ToRuntime(InRecord.Order, FallbackProductTable);
		return Delivery;
	}

	// 수첩 이벤트 기록을 레코드로 변환
	inline FCSJournalEventRecord ToRecord(const FCSJournalEvent& InEvent)
	{
		FCSJournalEventRecord Record;
		Record.EventId = InEvent.EventId;
		Record.BusinessDay = InEvent.BusinessDay;
		Record.Title = InEvent.Title;
		Record.Body = InEvent.Body;
		Record.bRead = InEvent.bRead;
		return Record;
	}

	// 수첩 이벤트 레코드를 런타임 데이터로 변환
	inline FCSJournalEvent ToRuntime(const FCSJournalEventRecord& InRecord)
	{
		FCSJournalEvent Event;
		Event.EventId = InRecord.EventId;
		Event.BusinessDay = InRecord.BusinessDay;
		Event.Title = InRecord.Title;
		Event.Body = InRecord.Body;
		Event.bRead = InRecord.bRead;
		return Event;
	}

	// ----- 상품 런타임 상태 -----

	// 상품 런타임 상태를 레코드로 변환
	// 레벨 재오픈 시 월드 시간이 0으로 초기화되므로 경과분을 반영한 현재 신선도를 기록
	inline FCSProductStateRecord ToRecord(
		const FCSProductRuntimeState& InState,
		int32 InCurrentFreshness)
	{
		FCSProductStateRecord Record;
		Record.bFreshnessInitialized = InState.bFreshnessInitialized;

		// 호출부가 경과분을 반영해 계산한 신선도를 0~100 범위로 제한해 기록
		// 감소 기준 시각은 이전 월드 시간 기준이므로 기록하지 않음
		Record.Freshness = FMath::Clamp(InCurrentFreshness, 0, 100);
		Record.bFreshnessDecayActive = InState.bFreshnessDecayActive;
		Record.bExpirationInitialized = InState.bExpirationInitialized;
		Record.ExpirationBusinessDay = InState.ExpirationBusinessDay;
		return Record;
	}

	// 상품 상태 레코드를 런타임 상태로 변환
	// 감소 중이던 상품은 현재 월드 시간을 새 기준 시각으로 설정
	inline FCSProductRuntimeState ToRuntime(
		const FCSProductStateRecord& InRecord,
		float CurrentWorldTimeSeconds)
	{
		FCSProductRuntimeState State;
		State.bFreshnessInitialized = InRecord.bFreshnessInitialized;
		State.Freshness = FMath::Clamp(InRecord.Freshness, 0, 100);
		State.bExpirationInitialized = InRecord.bExpirationInitialized;
		State.ExpirationBusinessDay = InRecord.ExpirationBusinessDay;

		// 감소 상태와 기준 시각을 초기화
		// StartFreshnessDecay()는 감소가 비활성일 때만 기준 시각을 설정
		State.bFreshnessDecayActive = false;
		State.FreshnessDecayStartTimeSeconds = 0.0f;

		// 감소 중이던 상품은 현재 월드 시간을 기준 시각으로 감소를 재개
		// 신선도가 0이면 감소를 재개하지 않음
		if (InRecord.bFreshnessDecayActive)
		{
			State.StartFreshnessDecay(CurrentWorldTimeSeconds);
		}

		return State;
	}
}
