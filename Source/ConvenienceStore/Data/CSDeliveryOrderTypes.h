#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSDeliveryOrderTypes.generated.h"

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSDeliveryRecipientInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Recipient")
	FName RecipientId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Recipient")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Recipient")
	FText RegionName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Recipient")
	FText FlavorText;
};

// 배달 주문의 현재 진행 상태입니다.
UENUM(BlueprintType)
enum class ECSDeliveryOrderStatus : uint8
{
	None,
	PendingAcceptance,
	Accepted,
	Completed,
	Declined,
	Expired,
	Canceled,
	Failed
};

// 정산 UI가 성공·일반 실패·기한 초과 실패를 서로 다르게 연출할 때 사용합니다.
UENUM(BlueprintType)
enum class ECSDeliverySettlementResult : uint8
{
	Completed,
	Failed,
	DeadlineExpired
};

// 배달 주문 한 줄에 필요한 상품과 수량입니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSDeliveryOrderLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order",
		meta = (RowType = "/Script/ConvenienceStore.CSProductDataRow"))
	FDataTableRowHandle ProductRow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery Order",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 Quantity = 1;
};

// UI와 마법진이 읽는 현재 배달 주문의 런타임 데이터입니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSDeliveryOrderRuntime
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	int32 OrderId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	ECSDeliveryOrderStatus Status = ECSDeliveryOrderStatus::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	TArray<FCSDeliveryOrderLine> RequiredProducts;

	// 월드 목적지 대신 주문에 포함되는 원격 수신처 정보입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order|Recipient")
	FName RecipientId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order|Recipient")
	FText RecipientDisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order|Recipient")
	FText RecipientRegionName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	int32 RewardGold = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order")
	int32 ReputationReward = 1;

	// 자정부터의 게임 분 기준으로, 이 시각까지 주문을 수락할 수 있습니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order|Time")
	float AcceptanceDeadlineMinutes = 0.0f;

	// 수락한 뒤 이 시각까지 배달을 완료해야 합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Delivery Order|Time")
	float DeliveryDeadlineMinutes = 0.0f;
};
