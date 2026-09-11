#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSEmployeeWaitingArea.generated.h"

class ACSStoreEmployeeCharacter;
class UArrowComponent;
class UBoxComponent;

// 창고 입구 등에 배치하는 직원 전용 대기 구역입니다. 화살표의 정면이 대기 중 바라볼 방향입니다.
UCLASS()
class CONVENIENCESTORE_API ACSEmployeeWaitingArea : public AActor
{
	GENERATED_BODY()

public:
	ACSEmployeeWaitingArea();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Employee|Waiting")
	int32 GetSlotCount() const { return FMath::Clamp(SlotCount, 1, 32); }
	bool GetSlotTransform(int32 SlotIndex, FTransform& OutTransform) const;
	bool CanReserveSlot(const ACSStoreEmployeeCharacter* Employee, int32 SlotIndex) const;
	bool TryReserveSlot(ACSStoreEmployeeCharacter* Employee, int32 SlotIndex);
	bool IsSlotReservedBy(const ACSStoreEmployeeCharacter* Employee, int32 SlotIndex) const;
	void ReleaseSlots(const ACSStoreEmployeeCharacter* Employee);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> FacingArrow;

	// 직원이 없어도 시설을 설치할 수 없는 구역입니다. 기존 건축 접근 영역 표시/검사를 공유합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> WaitingAccessZone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Waiting", meta = (ClampMin = "1", ClampMax = "32"))
	int32 SlotCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Waiting", meta = (ClampMin = "1", ClampMax = "32"))
	int32 SlotsPerRow = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Employee|Waiting", meta = (ClampMin = "120.0", Units = "cm"))
	float SlotSpacing = 180.0f;

private:
	TMap<int32, TWeakObjectPtr<ACSStoreEmployeeCharacter>> Reservations;
};
