#include "Features/Employees/CSRestockEmployeeCharacter.h"

ACSRestockEmployeeCharacter::ACSRestockEmployeeCharacter()
{
	// 역할 에셋을 아직 연결하지 않은 상태에서도 진열원은 계산 업무를 가져가지 않도록 기본 역할을 제한합니다.
	// 진열원이 진열할 수 없는 상자를 적합한 창고로 옮기는 업무까지 담당하도록 함께 허용합니다.
	FallbackSupportedTaskTypes = {
		ECSStoreEmployeeTaskType::RestockShelf,
		ECSStoreEmployeeTaskType::StoreCrate,
		ECSStoreEmployeeTaskType::StoreLooseProduct,
		ECSStoreEmployeeTaskType::ReorganizeMisplacedProduct,
		ECSStoreEmployeeTaskType::ProtectPerishableStock,
	};
	// 진열원은 역할 에셋을 잘못 연결해도 진열 업무만 수행해야 하므로 기본 업무 목록을 강제합니다.
	bLockToFallbackSupportedTaskTypes = true;
}
