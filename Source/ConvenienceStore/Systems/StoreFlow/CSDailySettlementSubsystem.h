#pragma once

#include "CoreMinimal.h"
#include "Data/CSFacilityOrderTypes.h"
#include "Data/CSOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSDailySettlementSubsystem.generated.h"

class ACSTrashCanActor;
class ACSCustomerCharacter;
class UCSCustomerRuntimeSubsystem;
class UCSOrderSubsystem;

// 폐기 시설과 정산 GameState 사이를 이벤트로 연결하며 UI에는 원본 데이터만 제공합니다.
UCLASS()
class CONVENIENCESTORE_API UCSDailySettlementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void HandleActorSpawned(AActor* SpawnedActor);
	void BindTrashCan(ACSTrashCanActor* TrashCan);

	UFUNCTION()
	void HandleItemDisposed(AActor* DisposedItem, int32 TotalDisposedCount);

	UFUNCTION()
	void HandleProductOrderPlaced(const FCSOrderResult& Result);

	UFUNCTION()
	void HandleFacilityOrderPlaced(const FCSFacilityOrderResult& Result);

	UFUNCTION()
	void HandleShopliftEscaped(ACSCustomerCharacter* Customer, int32 LostGold);

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACSTrashCanActor>> BoundTrashCans;

	UPROPERTY(Transient)
	TObjectPtr<UCSOrderSubsystem> OrderSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UCSCustomerRuntimeSubsystem> CustomerRuntimeSubsystem;

	FDelegateHandle ActorSpawnedDelegateHandle;
};
