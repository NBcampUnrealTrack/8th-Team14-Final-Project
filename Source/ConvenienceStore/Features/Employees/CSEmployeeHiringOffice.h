#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSEmployeeHiringTypes.h"
#include "CSEmployeeHiringOffice.generated.h"

class UArrowComponent;
class UStaticMeshComponent;
class ACSStoreEmployeeCharacter;
class UCSEmployeeHiringWidget;

// 레벨에 배치하면 기존 직원 BP 외형으로 세 지원자를 생성하는 작은 고용소입니다.
UCLASS()
class CONVENIENCESTORE_API ACSEmployeeHiringOffice : public AActor
{
	GENERATED_BODY()
public:
	ACSEmployeeHiringOffice();
protected:
	virtual void BeginPlay() override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Office") TObjectPtr<USceneComponent> SceneRoot;
	// 팀이 제작한 고용소 건물 메시를 BP에서 지정하는 자리입니다. 기본 메시를 두지 않습니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Office") TObjectPtr<UStaticMeshComponent> BuildingMesh;
	// 화살표는 직원 캡슐 중심입니다. 바닥 위 96cm가 기본값입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Office") TObjectPtr<UArrowComponent> FirstRestockPoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Office") TObjectPtr<UArrowComponent> CheckoutPoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Employee|Office") TObjectPtr<UArrowComponent> SecondRestockPoint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Office") TSoftClassPtr<ACSStoreEmployeeCharacter> RestockEmployeeClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Office") TSoftClassPtr<ACSStoreEmployeeCharacter> CheckoutEmployeeClass;
	// 지정하면 이 고용소에서 생성하는 세 지원자에게 같은 WBP를 적용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Office") TSoftClassPtr<UCSEmployeeHiringWidget> HiringWidgetClass;
private:
	void SpawnApplicant(ECSEmployeeHireSlot Slot, UArrowComponent* Point, TSubclassOf<ACSStoreEmployeeCharacter> EmployeeClass);
};
