#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSNoticeEventTypes.generated.h"

UENUM(BlueprintType)
enum class ECSNoticeEventSourceType : uint8
{
	News,
	Holiday
};

UENUM(BlueprintType)
enum class ECSNoticeEventTargetType : uint8
{
	Product,
	Category,
	// TargetId에 손님 Archetype 에셋 이름을 넣으면 그날 그 종족의 스폰 가중치를 바꾼다. 수요에는 영향 없음
	Race
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSNewsEventDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|News")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|News")
	FText Body;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|News",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SelectionWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|News")
	bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSHolidayEventDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Holiday",
		meta = (ClampMin = "1", ClampMax = "28", UIMin = "1", UIMax = "28"))
	int32 DayOfMonth = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Holiday")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Holiday")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Holiday")
	bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSNoticeEventEffectRuleRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Effect")
	ECSNoticeEventSourceType SourceType = ECSNoticeEventSourceType::News;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Effect")
	FName SourceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Effect")
	ECSNoticeEventTargetType TargetType = ECSNoticeEventTargetType::Product;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Effect")
	FName TargetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Effect")
	float DemandModifier = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice Event|Effect")
	bool bEnabled = true;
};

/** 게시판 뉴스 UI에 전달할 수요 변화 한 줄입니다. */
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSNoticeBoardDemandChange
{
	GENERATED_BODY()

	/** 상품명 또는 카테고리 표시 이름입니다. */
	UPROPERTY(BlueprintReadOnly, Category = "Notice Event|UI")
	FText TargetDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Notice Event|UI")
	ECSNoticeEventTargetType TargetType = ECSNoticeEventTargetType::Product;

	/** DT_ProductMaster의 RowName 또는 ECSProductCategory 값 이름입니다. */
	UPROPERTY(BlueprintReadOnly, Category = "Notice Event|UI")
	FName TargetId = NAME_None;

	/** 양수면 상승, 음수면 하락입니다. UI에는 수치를 표시하지 않아도 됩니다. */
	UPROPERTY(BlueprintReadOnly, Category = "Notice Event|UI")
	float DemandModifier = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Notice Event|UI")
	bool bIsIncrease = false;

	// 뉴스와 기념일 효과가 한 목록에 같이 나와서 어느 쪽 효과인지 구분하려고 둔다
	UPROPERTY(BlueprintReadOnly, Category = "Notice Event|UI")
	ECSNoticeEventSourceType SourceType = ECSNoticeEventSourceType::News;
};
