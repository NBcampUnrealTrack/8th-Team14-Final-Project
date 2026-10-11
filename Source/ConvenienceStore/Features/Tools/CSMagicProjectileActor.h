#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSMagicProjectileActor.generated.h"

class UProjectileMovementComponent;
class UPrimitiveComponent;
class USphereComponent;
class UNiagaraSystem;

// 지팡이가 발사하며, 피격 대상에 넉백을 전달하는 마법 투사체입니다.
UCLASS()
class CONVENIENCESTORE_API ACSMagicProjectileActor : public AActor
{
	GENERATED_BODY()

public:
	ACSMagicProjectileActor();

	// 발사 방향을 저장하여 피격 대상이 공격이 날아온 반대편으로 밀려나게 합니다.
	void InitializeProjectile(const FVector& InTravelDirection);

	// 발사자 외에 지팡이처럼 투사체가 통과해야 하는 액터를 충돌 대상에서 제외합니다.
	void AddIgnoredActor(AActor* ActorToIgnore);
	void SetEmployeeBoostDuration(float Duration) { EmployeeBoostDuration = FMath::Max(0.0f, Duration); }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleProjectileHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& HitResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// 충돌 지점에서 한 번 재생할 피격 나이아가라 시스템입니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic|Effects")
	TObjectPtr<UNiagaraSystem> ImpactNiagaraSystem;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic|Knockback",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CharacterKnockbackStrength = 1200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic|Knockback",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PhysicsImpulseStrength = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Magic|Lifetime",
		meta = (ClampMin = "0.1", UIMin = "0.1"))
	float ProjectileLifetime = 4.0f;

private:
	float EmployeeBoostDuration = 0.0f;
	void StartProjectileAfterEffectsReady();
	float EffectsReadyDeadline = 0.0f;

	FVector TravelDirection = FVector::ForwardVector;
};
