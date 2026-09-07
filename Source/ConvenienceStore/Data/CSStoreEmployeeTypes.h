#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CSStoreEmployeeTypes.generated.h"

class AActor;
class ACSStoreEmployeeCharacter;

// 직원이 처리할 수 있는 업무의 종류입니다. 새 업무를 추가할 때 이 열거형과 업무 생성 규칙만 확장하시면 됩니다.
UENUM(BlueprintType)
enum class ECSStoreEmployeeTaskType : uint8
{
	CheckoutService UMETA(DisplayName = "계산"),
	RestockShelf UMETA(DisplayName = "진열"),
	StoreCrate UMETA(DisplayName = "상자 보관"),
	StoreLooseProduct UMETA(DisplayName = "낱개 상품 보관"),
	ReorganizeMisplacedProduct UMETA(DisplayName = "오배치 상품 정리"),
	ProtectPerishableStock UMETA(DisplayName = "냉장·냉동 재고 보호"),
};

// 업무의 현재 진행 단계를 나타냅니다. UI나 디버그 화면에서 업무 상태를 그대로 표시할 수 있습니다.
UENUM(BlueprintType)
enum class ECSStoreEmployeeTaskStatus : uint8
{
	Pending UMETA(DisplayName = "대기"),
	InProgress UMETA(DisplayName = "진행 중"),
	Completed UMETA(DisplayName = "완료"),
	Failed UMETA(DisplayName = "실패"),
	Cancelled UMETA(DisplayName = "취소"),
};

// 서브시스템이 생성하고 직원 AI가 수령하는 공통 업무 단위입니다.
USTRUCT(BlueprintType)
struct FCSStoreEmployeeTask
{
	GENERATED_BODY()

	// 업무를 구별하는 실행 중 고유 번호입니다. 저장 데이터가 필요해지면 GUID로 교체해도 구조를 유지할 수 있습니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	int32 TaskId = INDEX_NONE;

	// 직원 역할이 이 업무를 처리할 수 있는지 판단하는 업무 종류입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	ECSStoreEmployeeTaskType Type = ECSStoreEmployeeTaskType::CheckoutService;

	// 같은 종류의 업무가 여러 개일 때 먼저 처리할 우선순위입니다. 값이 클수록 먼저 배정됩니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	int32 Priority = 0;

	// 계산대 또는 진열대처럼 실제 업무를 수행할 대상 액터입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	TObjectPtr<AActor> TargetActor = nullptr;

	// 진열·보관 업무에서 상품 또는 배송 상자를 가져올 원본 액터입니다. 계산 업무에서는 비어 있습니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	TObjectPtr<AActor> SourceActor = nullptr;

	// 재배치 업무에서 상품이 원래 놓여 있던 진열대입니다.
	// 해당 진열대의 모든 오배치 상품이 정리될 때까지 새 상품 보충 업무를 만들지 않는 데 사용합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	TObjectPtr<AActor> SourceFacilityActor = nullptr;

	// 진열 업무에서 채워야 할 상품의 데이터 행입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	FDataTableRowHandle ProductRow;

	// 이번 업무가 채우려는 최대 수량입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	int32 RequestedQuantity = 1;

	// 모든 미설정 단에 고유 상품을 먼저 배치한 뒤 남는 자리를 채우는 보조 보충인지 기록합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	bool bAllowCrossTierDuplicateFallback = false;

	// 현재 이 업무를 처리 중인 직원입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	TObjectPtr<ACSStoreEmployeeCharacter> AssignedEmployee = nullptr;

	// 업무의 진행 상태입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Task")
	ECSStoreEmployeeTaskStatus Status = ECSStoreEmployeeTaskStatus::Pending;
};
