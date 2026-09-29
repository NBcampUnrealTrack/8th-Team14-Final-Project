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
class ACSGachaSeal;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;

// 레벨에 직접 배치하고 자식 블루프린트에서 외형과 연출을 바꾸실 수 있습니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSGachaMachine : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSGachaMachine();
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UCSGachaPool> RewardPool;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TSubclassOf<ACSProductPickupActor> ProductActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TSubclassOf<ACSGachaArtifactPickup> ArtifactActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha") TSubclassOf<ACSGachaSeal> SealActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects") TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects") bool bUseDemoEffects = true;
	// 메시를 지정하면 기본 도형 마법진 대신 사용합니다. 수평인 Plane 메시도 지정하실 수 있습니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects|Circle") TObjectPtr<UStaticMesh> CircleMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects|Circle") TObjectPtr<UMaterialInterface> CircleMaterialOverride;
	// 사용자 머티리얼의 발광 강도에 연결된 스칼라 파라미터 이름을 입력해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects|Circle") FName CircleGlowParameterName = TEXT("Glow");
	// 반경에 맞춘 기본 크기에 추가로 적용할 위치·회전·크기입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects|Circle") FTransform CircleMeshTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects|Circle") bool bShowMagicCircle = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects") bool bShowBeam = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects", meta=(ClampMin="10")) float CircleRadius = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects", meta=(ClampMin="50")) float BeamHeight = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha|Effects", meta=(ClampMin="0.01")) float BeamToSealDelay = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha", meta = (ClampMin = "0.0", UIMin = "0.0")) float DrawDelaySeconds = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha", meta = (ClampMin = "0.0")) float LaunchImpulse = 450.0f;

	// 추첨이 시작되면 BP에서 기계 작동 연출을 재생하실 수 있습니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Gacha") void OnDrawStarted();
	UFUNCTION(BlueprintImplementableEvent, Category = "Gacha") void OnBeamFinished();
	UFUNCTION(BlueprintImplementableEvent, Category = "Gacha") void OnSealDispensed();
	// 봉인석이 열린 뒤에만 결과를 전달하여 개봉 전에 내용물이 표시되지 않게 합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Gacha") void OnRewardDispensed(ECSGachaRewardType Type, FName RewardId);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UBoxComponent> InteractionBox;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UStaticMeshComponent> DisplayMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<USceneComponent> RewardSpawnPoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha|Effects") TObjectPtr<UInstancedStaticMeshComponent> MagicCircle;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha|Effects") TObjectPtr<UStaticMeshComponent> CustomMagicCircle;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha|Effects") TObjectPtr<UStaticMeshComponent> Beam;

private:
	void CompleteDraw();
	void FinishBeam();
	UFUNCTION() void HandleRewardRevealed(ECSGachaRewardType Type, FName RewardId);
	bool IsRewardAvailable(const FCSGachaPoolRow& Reward) const;
	void NotifyPlayer(const FText& Message) const;
	bool HasPendingReward() const;
	TArray<FCSGachaPoolRow> BuildRewards() const;

	UPROPERTY(Transient) TObjectPtr<AActor> PendingReward;
	UPROPERTY(Transient) TObjectPtr<ACSPlayerCharacter> DrawingPlayer;
	FCSGachaPoolRow SelectedReward;
	FTimerHandle DrawTimer;
	bool bDrawing = false;
	bool bBeamActive = false;
	float EffectElapsed = 0.f;
	float DispensePulseRemaining = 0.f;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CircleMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BeamMaterial;
};
