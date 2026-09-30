#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Features/Facilities/CSBuildableFacility.h"
#include "GameFramework/Actor.h"
#include "CSTrashCanActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UCSSaveIdComponent;
class UArrowComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FCSOnItemDisposed,
	AActor*,
	DisposedItem,
	int32,
	TotalDisposedCount);

UCLASS()
class CONVENIENCESTORE_API ACSTrashCanActor : public AActor,
	public ICSBuildableFacility
{
	GENERATED_BODY()

public:
	ACSTrashCanActor();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UArrowComponent> FairyApproachPoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UArrowComponent> FairyApproachBack;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UArrowComponent> FairyApproachLeft;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UArrowComponent> FairyApproachRight;
	TArray<UArrowComponent*> GetFairyApproachPoints() const;
	// 운반하는 쓰레기가 입구 중앙 위에 오도록 위치를 조절해 주세요.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<UArrowComponent> FairyDropPoint;
	bool IsAvailableForFairy() const;

	virtual FName GetFacilityId_Implementation() const override;
	virtual bool IsFacilityInstalled_Implementation() const override;
	virtual void InitializeAsUninstalledFacility_Implementation() override;
	virtual void PrepareForBuildPickup_Implementation() override;
	virtual void FinalizeBuildPlacement_Implementation() override;

	UFUNCTION(BlueprintPure, Category = "Store|TrashCan")
	bool CanDisposeItem(AActor* ItemActor) const;

	UFUNCTION(BlueprintCallable, Category = "Store|TrashCan")
	bool TryDisposeItem(AActor* ItemActor);

	// 물건이 버려질 때마다 방송
	// 튜토리얼을 포함해 관심 있는 누구든 구독 가능
	UPROPERTY(BlueprintAssignable, Category = "Store|TrashCan")
	FCSOnItemDisposed OnItemDisposed;

	UFUNCTION(BlueprintPure, Category = "Store|TrashCan")
	int32 GetTotalDisposedCount() const
	{
		return TotalDisposedCount;
	}

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TrashCanMesh;

	// 세이브 복원 시 이 쓰레기통을 다시 찾기 위한 안정 식별자
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCSSaveIdComponent> SaveIdComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility",
		meta = (RowType = "/Script/ConvenienceStore.CSFacilityDataRow"))
	FDataTableRowHandle FacilityRow;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Facility|Build")
	bool bIsInstalled = true;

	UPROPERTY(Transient)
	FTransform TrashCanMeshRelativeTransformBeforePhysics = FTransform::Identity;

	UPROPERTY(Transient)
	bool bHasCachedTrashCanMeshRelativeTransform = false;

private:
	void SetFacilityPhysicsEnabled(bool bEnabled);

	int32 TotalDisposedCount = 0;
};
