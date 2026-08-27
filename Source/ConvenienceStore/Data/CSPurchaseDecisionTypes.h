#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/CSProductTypes.h"
#include "CSPurchaseDecisionTypes.generated.h"

class UTexture2D;

// 손님이 살지 말지 고민하는 후보 상품 하나를 표현하는 구조체
USTRUCT(BlueprintType)
struct FCSPurchaseCandidate
{
	GENERATED_BODY()

	// 상품 마스터 데이터테이블(DT_FantasyProductMaster)의 행을 가리키는 핸들
	// 가격, 카테고리 등 상품 고유 정보는 전부 여기서 조회
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Purchase")
	FDataTableRowHandle ProductRowHandle;

	// 실제로 진열대 위에 놓여 있는 상품 액터
	// 손님이 이 후보를 선택하면 이 액터를 TakeProduct()에 넘겨 집게 됨
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Purchase")
	TObjectPtr<AActor> ProductActor = nullptr;

	// 이 후보에 매겨진 0~1 점수
	UPROPERTY(BlueprintReadOnly, Category = "Purchase")
	float Score = 0.f;
};

// 손님이 구매를 거절한 사유
// 거절 연출(디버그 텍스트/속마음)이 이 값으로 어떤 문구를 보여줄지 결정함
UENUM(BlueprintType)
enum class ECSPurchaseRejectReason : uint8
{
	None,
	PriceRejected,       // 가격이 너무 비쌈 (가격 하드컷, 또는 최소구매임계치 미달의 실제 원인이 가격일 때)
	CategoryBlocked,     // 이 종족이 절대 안 사는 카테고리
	HardWalletExceeded,  // 하드 지갑(물리적으로 낼 수 있는 최대 금액) 초과
	ShelfEmpty           // 진열대에 재고가 없음
};

// 거절 사유 상세 정보. UI 연출(디버그 텍스트/속마음 위젯)이 이 값을 읽어 문구를 조합한다
USTRUCT(BlueprintType)
struct FCSPurchaseRejectionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Purchase|Rejection")
	ECSPurchaseRejectReason Reason = ECSPurchaseRejectReason::None;

	// 거절당한 상품의 ID(DT_ProductMaster의 RowName). ShelfEmpty일 때는 NAME_None으로 남음
	UPROPERTY(BlueprintReadOnly, Category = "Purchase|Rejection")
	FName ProductId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Purchase|Rejection")
	float ActualPrice = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Purchase|Rejection")
	float ReferencePrice = 0.f;

	// CategoryBlocked일 때만 의미 있음
	UPROPERTY(BlueprintReadOnly, Category = "Purchase|Rejection")
	ECSProductCategory BlockedCategory = ECSProductCategory::Beverage;

	// 0=아슬아슬, 1=터무니없음. CategoryBlocked는 항상 1.0 고정(대표 사유 비교에 안 쓰임)
	UPROPERTY(BlueprintReadOnly, Category = "Purchase|Rejection")
	float Severity = 0.f;
};

// 거절 사유 하나가 강도(Severity)에 따라 여러 단계로 나뉠 때 한 단계를 표현
// 예: PriceRejected 사유 안에서도 "너무 비쌈"(약함)과 "화남"(강함)이 서로 다른 Tier
USTRUCT(BlueprintType)
struct FCSPurchaseRejectionTier
{
	GENERATED_BODY()

	// 이 구간이 적용되는 최소 Severity. FCSPurchaseRejectionDisplayRow::Tiers는
	// 이 값 내림차순으로 등록되어 있어야 GetRejectionDisplayTier()가 올바르게 동작함
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Purchase|Rejection",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeverityThreshold = 0.f;

	// 거절 즉시 손님 머리 위에 5초간 뜨는 디버그 텍스트 (예: "너무 비쌈")
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Purchase|Rejection")
	FText DebugText;

	// 그 표시가 떠 있는 동안 마법봉으로 맞혔을 때 뜨는 속마음 문장
	// {ActualPrice}/{ReferencePrice} 자리표시자를 쓰면 실제 가격이 채워짐
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Purchase|Rejection")
	FText InnerThoughtTemplate;

	// 지금은 비워둠 - 나중에 아이콘 연출을 붙일 자리
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Purchase|Rejection")
	TObjectPtr<UTexture2D> Icon = nullptr;
};

// 거절 사유(ECSPurchaseRejectReason) 하나에 대응하는 DataTable 행
// 문장/강도 구간을 전부 DT로 빼서 재컴파일 없이 디자이너가 손볼 수 있게 함
USTRUCT(BlueprintType)
struct FCSPurchaseRejectionDisplayRow : public FTableRowBase
{
	GENERATED_BODY()

	// Severity 내림차순으로 등록할 것
	// GetRejectionDisplayTier()가 위에서부터 훑어 Severity >= Threshold인 첫 구간을 반환하고
	// (없으면 마지막 구간을 반환)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Purchase|Rejection")
	TArray<FCSPurchaseRejectionTier> Tiers;
};
