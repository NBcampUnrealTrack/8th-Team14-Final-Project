#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "Data/CSGachaPool.h"
#include "CSGachaMachine.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USceneComponent;
class ACSProductPickupActor;
class ACSGachaArtifactPickup;
class ACSPlayerCharacter;

// 레벨에 직접 배치하고 자식 블루프린트에서 외형과 연출을 바꾸실 수 있습니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSGachaMachine : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSGachaMachine();
	virtual void BeginPlay() override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UCSGachaPool> RewardPool;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TSubclassOf<ACSProductPickupActor> ProductActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TSubclassOf<ACSGachaArtifactPickup> ArtifactActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha", meta = (ClampMin = "0.0", UIMin = "0.0")) float DrawDelaySeconds = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha", meta = (ClampMin = "0.0")) float LaunchImpulse = 450.0f;

	// 추첨이 시작되면 BP에서 기계 작동 연출을 재생하실 수 있습니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Gacha") void OnDrawStarted();
	// 보상이 배출되면 BP에서 결과 연출을 재생하실 수 있습니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Gacha") void OnRewardDispensed(ECSGachaRewardType Type, FName RewardId);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UBoxComponent> InteractionBox;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UStaticMeshComponent> DisplayMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<USceneComponent> RewardSpawnPoint;

private:
	void CompleteDraw();
	bool IsRewardAvailable(const FCSGachaPoolRow& Reward) const;
	void NotifyPlayer(const FText& Message) const;
	bool HasPendingReward() const;
	TArray<FCSGachaPoolRow> BuildRewards() const;

	UPROPERTY(Transient) TObjectPtr<AActor> PendingReward;
	UPROPERTY(Transient) TObjectPtr<ACSPlayerCharacter> DrawingPlayer;
	FCSGachaPoolRow SelectedReward;
	FTimerHandle DrawTimer;
	bool bDrawing = false;
};
