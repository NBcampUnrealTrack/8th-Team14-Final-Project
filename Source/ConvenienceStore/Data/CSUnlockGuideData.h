#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSUnlockGuideData.generated.h"

USTRUCT(BlueprintType)
struct FCSUnlockGuideEntry
{
	GENERATED_BODY()
	// 저장에 사용하는 고유 ID입니다. 이미 배포한 ID는 변경하지 말아 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1")) int32 RequiredStoreLevel = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DisplayOrder = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (MultiLine = "true")) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (MultiLine = "true")) FText UsageLocation;
};

// 카메라가 없는 해금 항목도 이 데이터 에셋에 등록하시면 설명을 제공할 수 있습니다.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSUnlockGuideData : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock Guide") TArray<FCSUnlockGuideEntry> Entries;
};
