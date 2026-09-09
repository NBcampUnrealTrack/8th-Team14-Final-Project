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
	Category
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
