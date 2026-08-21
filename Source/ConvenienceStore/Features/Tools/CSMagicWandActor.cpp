#include "Features/Tools/CSMagicWandActor.h"

#include "Features/Tools/CSMagicProjectileActor.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Systems/Audio/CSAudioSubsystem.h"

namespace
{
void PlayMagicWandGameplaySFX(const AActor* WorldContextActor, ECSAudioEvent AudioEvent)
{
	if (!IsValid(WorldContextActor))
	{
		return;
	}

	if (UGameInstance* GameInstance = WorldContextActor->GetGameInstance();
		IsValid(GameInstance))
	{
		if (UCSAudioSubsystem* AudioSubsystem =
			GameInstance->GetSubsystem<UCSAudioSubsystem>(); IsValid(AudioSubsystem))
		{
			AudioSubsystem->PlaySFX(AudioEvent);
		}
	}
}
}

ACSMagicWandActor::ACSMagicWandActor()
{
	ToolDisplayName = NSLOCTEXT("Tool", "MagicWandName", "마법 지팡이");

	Muzzle = CreateDefaultSubobject<USceneComponent>(TEXT("Muzzle"));
	Muzzle->SetupAttachment(ToolMesh);
	// 지팡이 모델이 없을 때도 발사 위치를 확인할 수 있도록 기본값을 앞쪽에 둡니다.
	Muzzle->SetRelativeLocation(FVector(60.0f, 0.0f, 0.0f));
}

bool ACSMagicWandActor::RememberHomeDockFromCurrentAttachment()
{
	USceneComponent* ExistingHomeDock = HomeDockComponent.Get();
	AActor* ExistingHomeOwner = HomeDockOwner.Get();
	if (IsValid(ExistingHomeDock) &&
		IsValid(ExistingHomeOwner) &&
		ExistingHomeDock->GetOwner() == ExistingHomeOwner)
	{
		return true;
	}

	USceneComponent* WandRoot = GetRootComponent();
	USceneComponent* CurrentAttachParent = IsValid(WandRoot)
		? WandRoot->GetAttachParent()
		: nullptr;
	AActor* CurrentAttachOwner = IsValid(CurrentAttachParent)
		? CurrentAttachParent->GetOwner()
		: nullptr;
	if (!IsValid(CurrentAttachParent) || !IsValid(CurrentAttachOwner))
	{
		return false;
	}

	HomeDockComponent = CurrentAttachParent;
	HomeDockOwner = CurrentAttachOwner;
	return true;
}

USceneComponent* ACSMagicWandActor::GetHomeDockComponent() const
{
	USceneComponent* HomeDock = HomeDockComponent.Get();
	AActor* HomeOwner = HomeDockOwner.Get();
	return IsValid(HomeDock) && IsValid(HomeOwner) &&
		HomeDock->GetOwner() == HomeOwner
		? HomeDock
		: nullptr;
}

AActor* ACSMagicWandActor::GetHomeDockOwner() const
{
	return GetHomeDockComponent() != nullptr
		? HomeDockOwner.Get()
		: nullptr;
}

bool ACSMagicWandActor::UsePrimary_Implementation(AActor* User)
{
	if (!bIsEquipped || !IsValid(User) || MagicProjectileClass == nullptr || GetWorld() == nullptr)
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastPrimaryUseTime < PrimaryUseCooldown)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	User->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	const FVector TravelDirection = ViewRotation.Vector().GetSafeNormal();
	const FVector SpawnLocation = IsValid(Muzzle)
		? Muzzle->GetComponentLocation()
		: ViewLocation + TravelDirection * 60.0f;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = User;
	SpawnParameters.Instigator = Cast<APawn>(User);
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACSMagicProjectileActor* Projectile = GetWorld()->SpawnActor<ACSMagicProjectileActor>(
		MagicProjectileClass,
		SpawnLocation,
		TravelDirection.Rotation(),
		SpawnParameters);
	if (!IsValid(Projectile))
	{
		return false;
	}

	// 지팡이 끝에서 생성된 투사체가 지팡이 자체 충돌에 막히지 않도록 제외합니다.
	Projectile->AddIgnoredActor(this);

	// 투사체가 카메라 조준 방향을 그대로 사용하도록 초기 속도와 방향을 전달합니다.
	Projectile->InitializeProjectile(TravelDirection);
	LastPrimaryUseTime = CurrentTime;
	PlayMagicWandGameplaySFX(this, ECSAudioEvent::MagicAttack);
	return true;
}
