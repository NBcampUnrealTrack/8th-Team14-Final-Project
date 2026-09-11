#include "Features/Employees/CSCheckoutEmployeeCharacter.h"

ACSCheckoutEmployeeCharacter::ACSCheckoutEmployeeCharacter()
{
	// 역할 에셋을 아직 연결하지 않은 상태에서도 계산원은 진열 업무를 가져가지 않도록 기본 역할을 제한합니다.
	FallbackSupportedTaskTypes = { ECSStoreEmployeeTaskType::CheckoutService };
	// 계산원은 역할 에셋을 잘못 연결해도 계산 업무만 수행해야 하므로 기본 업무 목록을 강제합니다.
	bLockToFallbackSupportedTaskTypes = true;
	bWaitAtCheckoutCounter = true;
	IdleReturnDelaySeconds = 0.0f;
}
