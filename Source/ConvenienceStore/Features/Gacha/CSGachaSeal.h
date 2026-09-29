#pragma once

#include "CoreMinimal.h"
#include "Data/CSGachaDataTypes.h"
#include "Features/Tools/CSUsableToolActor.h"
#include "CSGachaSeal.generated.h"

class ACSProductPickupActor;
class ACSGachaArtifactPickup;
class UPointLightComponent;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCSGachaSealOpened, ECSGachaRewardType, Type, FName, RewardId);

// 추첨 결과를 보관하고 플레이어가 봉인을 해제할 때 실물 보상을 생성합니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSGachaSeal : public ACSUsableToolActor
{
	GENERATED_BODY()
public:
	ACSGachaSeal();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnEquipped(ACSPlayerCharacter* NewOwner) override;
	virtual void OnReleasedToWorld(bool bThrown) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;
	void InitializeReward(const FCSGachaPoolRow& InReward, TSubclassOf<ACSProductPickupActor> InProductClass, TSubclassOf<ACSGachaArtifactPickup> InArtifactClass);
	void Launch(const FVector& Velocity);
	bool IsOpened() const { return bOpened; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Seal|Visual") TObjectPtr<UStaticMesh> SealMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Seal|Visual") TObjectPtr<UStaticMesh> FragmentMesh;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Seal|Visual") TObjectPtr<UMaterialInterface> SealMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Seal|Visual") TObjectPtr<USoundBase> BreakSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Seal|Visual", meta=(ClampMin="0.2")) float BreakDuration = 0.7f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Seal|Visual") bool bUseDemoFragments = true;
	// 정식 파편·나이아가라 연출은 이 이벤트에서 추가하실 수 있습니다.
	UFUNCTION(BlueprintImplementableEvent, Category="Seal") void OnSealOpened(bool bThrown);
	UPROPERTY(BlueprintAssignable, Category="Seal") FCSGachaSealOpened OnRewardRevealed;

private:
	void OpenSeal();
	AActor* SpawnReward(FText& OutFailure);
	UFUNCTION() void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
	UPROPERTY(Transient) FCSGachaPoolRow Reward;
	UPROPERTY(Transient) TSubclassOf<ACSProductPickupActor> ProductClass;
	UPROPERTY(Transient) TSubclassOf<ACSGachaArtifactPickup> ArtifactClass;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Fragments;
	UPROPERTY(VisibleAnywhere, Category="Seal") TObjectPtr<UPointLightComponent> Flash;
	TWeakObjectPtr<ACSPlayerCharacter> LastHolder;
	FTimerHandle OpenTimer;
	FVector OpenOrigin = FVector::ZeroVector;
	FVector ImpactNormal = FVector::UpVector;
	FVector RecoveryLocation = FVector::ZeroVector;
	float BreakElapsed = 0.f;
	bool bHasReward = false;
	bool bAwaitingImpact = false;
	bool bOpened = false;
	bool bOpening = false;
	bool bThrownOpen = false;
};
