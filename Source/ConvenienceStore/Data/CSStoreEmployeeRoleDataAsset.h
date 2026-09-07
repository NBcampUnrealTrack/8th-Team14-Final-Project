#pragma once

#include "CoreMinimal.h"
#include "CSStoreEmployeeTypes.h"
#include "Engine/DataAsset.h"
#include "CSStoreEmployeeRoleDataAsset.generated.h"

// 직원별 역할과 업무 시간을 데이터로 분리하여, 코드 수정 없이 신규 직원 유형을 만들 수 있게 하는 에셋입니다.
UCLASS(BlueprintType)
class CONVENIENCESTORE_API UCSStoreEmployeeRoleDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// 게임 화면과 관리 UI에 표시할 역할 이름입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Role")
	FText RoleDisplayName;

	// 이 역할이 수령할 수 있는 업무 종류 목록입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Role")
	TArray<ECSStoreEmployeeTaskType> SupportedTaskTypes;

	// 대상 위치에 도착한 뒤 실제 업무를 처리하는 데 걸리는 시간입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Work", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WorkDurationSeconds = 1.5f;
};
