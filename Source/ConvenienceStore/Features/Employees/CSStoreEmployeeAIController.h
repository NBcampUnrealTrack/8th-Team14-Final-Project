#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CSStoreEmployeeAIController.generated.h"

class UStateTreeAIComponent;

// 직원 캐릭터의 이동을 담당하고, 나중에 역할별 StateTree를 연결할 수 있도록 준비한 기본 AI 컨트롤러입니다.
UCLASS()
class CONVENIENCESTORE_API ACSStoreEmployeeAIController : public AAIController
{
	GENERATED_BODY()

public:
	ACSStoreEmployeeAIController();

	// 블루프린트에서 역할별 StateTree를 붙일 때 사용할 컴포넌트를 반환합니다.
	UStateTreeAIComponent* GetStateTreeAI() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|AI")
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;
};
