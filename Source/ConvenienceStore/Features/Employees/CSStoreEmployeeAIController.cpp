#include "Features/Employees/CSStoreEmployeeAIController.h"

#include "Components/StateTreeAIComponent.h"

ACSStoreEmployeeAIController::ACSStoreEmployeeAIController()
{
	// 현재는 C++ 업무 드라이버가 동작하지만, 역할별 세부 행동을 StateTree로 분리할 수 있도록 미리 컴포넌트를 둡니다.
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
}

UStateTreeAIComponent* ACSStoreEmployeeAIController::GetStateTreeAI() const
{
	return StateTreeAI;
}
