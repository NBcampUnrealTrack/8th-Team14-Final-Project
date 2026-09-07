#pragma once

#include "CoreMinimal.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "CSRestockEmployeeCharacter.generated.h"

// 진열 보충 업무만 수령하도록 기본 설정된 직원입니다. 블루프린트에서는 이 클래스를 부모로 지정하시면 됩니다.
UCLASS()
class CONVENIENCESTORE_API ACSRestockEmployeeCharacter : public ACSStoreEmployeeCharacter
{
	GENERATED_BODY()

public:
	ACSRestockEmployeeCharacter();
};
