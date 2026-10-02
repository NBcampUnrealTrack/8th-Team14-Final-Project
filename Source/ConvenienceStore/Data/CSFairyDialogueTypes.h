#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSFairyDialogueTypes.generated.h"

UENUM(BlueprintType)
enum class ECSFairyDialogueGroup : uint8
{
	BeforeContract,
	ContractReady,
	Introduction,
	AfterContract,
	Working
};

// 대사 한 개를 한 행으로 추가해 주세요. 같은 그룹의 일반 대사는 무작위로 선택합니다.
USTRUCT(BlueprintType)
struct CONVENIENCESTORE_API FCSFairyDialogueRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ECSFairyDialogueGroup Group = ECSFairyDialogueGroup::BeforeContract;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MultiLine=true)) FText Text;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float DisplayDuration = 5.f;
};
