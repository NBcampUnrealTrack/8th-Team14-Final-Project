#include "Features/Tools/CSMagicProjectileActor.h"

#include "Components/SphereComponent.h"
#include "Features/Customers/CSCustomerCharacter.h"
#include "Features/Employees/CSStoreEmployeeCharacter.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Features/Tools/CSHitReactable.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

ACSMagicProjectileActor::ACSMagicProjectileActor()
{
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(20.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetGenerateOverlapEvents(false);
	CollisionSphere->OnComponentHit.AddDynamic(this, &ACSMagicProjectileActor::HandleProjectileHit);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->InitialSpeed = 2200.0f;
	ProjectileMovement->MaxSpeed = 2200.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
}

void ACSMagicProjectileActor::BeginPlay()
{
	Super::BeginPlay();
	// 캐릭터끼리 통과하더라도 마법 피격은 프로젝트의 전용 투사체 채널로 판정합니다.
	CollisionSphere->SetCollisionObjectType(ECC_GameTraceChannel1);
	SetLifeSpan(ProjectileLifetime);

	// 발사자와 투사체가 즉시 충돌하지 않도록 소유자를 충돌 대상에서 제외합니다.
	if (AActor* ProjectileOwner = GetOwner())
	{
		CollisionSphere->IgnoreActorWhenMoving(ProjectileOwner, true);
	}
}

void ACSMagicProjectileActor::InitializeProjectile(const FVector& InTravelDirection)
{
	TravelDirection = InTravelDirection.GetSafeNormal();
	if (TravelDirection.IsNearlyZero())
	{
		TravelDirection = GetActorForwardVector();
	}

	ProjectileMovement->Velocity = TravelDirection * ProjectileMovement->InitialSpeed;
	SetActorRotation(TravelDirection.Rotation());
	ProjectileMovement->SetComponentTickEnabled(false);
	EffectsReadyDeadline = GetWorld()->GetTimeSeconds() + 0.25f;
	GetWorldTimerManager().SetTimerForNextTick(this, &ACSMagicProjectileActor::StartProjectileAfterEffectsReady);
}

void ACSMagicProjectileActor::StartProjectileAfterEffectsReady()
{
	TInlineComponentArray<UNiagaraComponent*> ProjectileEffects(this);
	for (const UNiagaraComponent* Effect : ProjectileEffects)
	{
		// The first activation can be deferred while Niagara/PSOs become ready.
		// Bound the wait so a culled effect cannot prevent projectile gameplay.
		if (Effect->GetAsset() && Effect->bAutoActivate &&
			(!Effect->IsActive() || !Effect->GetSystemInstanceController()) &&
			GetWorld()->GetTimeSeconds() < EffectsReadyDeadline)
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &ACSMagicProjectileActor::StartProjectileAfterEffectsReady);
			return;
		}
	}

	// Seed the location-event ribbon at the muzzle before the first movement tick.
	// Otherwise its first particles are created after the fast projectile has moved.
	for (UNiagaraComponent* Effect : ProjectileEffects)
	{
		if (Effect->IsActive())
		{
			Effect->AdvanceSimulation(3, 1.0f / 60.0f);
		}
	}
	// Keep the seeded effect at the muzzle for its first rendered frame.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ProjectileMovement->SetComponentTickEnabled(true);
	}));
}

void ACSMagicProjectileActor::AddIgnoredActor(AActor* ActorToIgnore)
{
	if (IsValid(ActorToIgnore))
	{
		// 지팡이 메시가 충돌을 사용하더라도 투사체가 발사 직후 막히지 않게 합니다.
		CollisionSphere->IgnoreActorWhenMoving(ActorToIgnore, true);
	}
}

void ACSMagicProjectileActor::HandleProjectileHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& HitResult)
{
	if (!IsValid(OtherActor) || OtherActor == GetOwner())
	{
		return;
	}

	// 캐릭터처럼 피격 반응을 구현한 대상은 각자의 방식으로 넉백을 처리합니다.
	if (OtherActor->Implements<UCSHitReactable>())
	{
		if (ACSStoreEmployeeCharacter* Employee = Cast<ACSStoreEmployeeCharacter>(OtherActor))
		{
			if (EmployeeBoostDuration > 0.0f) Employee->ApplyMagicWorkBoost(EmployeeBoostDuration, 1.5f);
		}
		else ICSHitReactable::Execute_ApplyMagicHit(
			OtherActor,
			HitResult.ImpactPoint,
			TravelDirection,
			CharacterKnockbackStrength);
		// 같은 투사체의 뒤늦은 중복 Hit 콜백에서는 추가 재생하지 않습니다.
		if ((Cast<ACSCustomerCharacter>(OtherActor) || Cast<ACSStoreEmployeeCharacter>(OtherActor)) && !IsActorBeingDestroyed())
			if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::MagicHitCustomer);
	}
	else if (IsValid(OtherComponent) && OtherComponent->IsSimulatingPhysics())
	{
		// 물리 오브젝트는 실제 맞은 위치에 임펄스를 가해 회전도 자연스럽게 반영합니다.
		OtherComponent->AddImpulseAtLocation(
			TravelDirection * PhysicsImpulseStrength,
			HitResult.ImpactPoint);
	}

	if (IsValid(ImpactNiagaraSystem))
	{
		// 표면 법선을 회전값으로 사용해 피격 효과가 맞은 면 바깥쪽을 향하게 합니다.
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			ImpactNiagaraSystem,
			HitResult.ImpactPoint,
			HitResult.ImpactNormal.Rotation());
	}

	Destroy();
}
