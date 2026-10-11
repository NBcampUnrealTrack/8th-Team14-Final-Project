#include "Features/Tools/CSMagicWandActor.h"

#include "Features/Tools/CSMagicProjectileActor.h"
#include "Features/Tools/CSMagicWandCrosshairWidget.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

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

void ACSMagicWandActor::OnEquipped(ACSPlayerCharacter* NewOwner)
{
	Super::OnEquipped(NewOwner);
	if (IsValid(NewOwner))
	{
		if (!CachedWorldScale.IsSet())
		{
			CachedWorldScale = GetActorScale3D();
		}
		SetActorScale3D(CachedWorldScale.GetValue() *
			FMath::Max(0.01f, NewOwner->GetMagicWandHeldScaleMultiplier()));

		ShowCrosshairWidget(NewOwner);
	}
}

void ACSMagicWandActor::OnUnequipped()
{
	HideCrosshairWidget();
	RestoreWorldScale();
	Super::OnUnequipped();
}

void ACSMagicWandActor::ShowCrosshairWidget(ACSPlayerCharacter* NewOwner)
{
	if (CrosshairWidgetClass == nullptr || IsValid(ActiveCrosshairWidget))
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(NewOwner->GetController());
	if (!IsValid(PlayerController))
	{
		return;
	}

	ActiveCrosshairWidget = CreateWidget<UCSMagicWandCrosshairWidget>(PlayerController, CrosshairWidgetClass);
	if (IsValid(ActiveCrosshairWidget))
	{
		ActiveCrosshairWidget->AddToViewport();
		// 마법봉 전용 조준점과 겹치지 않도록 HUD 기본 조준점을 잠깐 숨깁니다.
		if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(PlayerController))
		{
			StoreController->SetDefaultCrosshairVisible(false);
		}
	}
}

void ACSMagicWandActor::HideCrosshairWidget()
{
	if (IsValid(ActiveCrosshairWidget))
	{
		if (ACSStorePlayerController* StoreController =
			Cast<ACSStorePlayerController>(ActiveCrosshairWidget->GetOwningPlayer()))
		{
			StoreController->SetDefaultCrosshairVisible(true);
		}
		ActiveCrosshairWidget->RemoveFromParent();
	}
	ActiveCrosshairWidget = nullptr;
}

void ACSMagicWandActor::PrepareForPickupPhysics()
{
	Super::PrepareForPickupPhysics();
	// Quickbar 보관과 Home Dock 반환도 이 기존 준비 경로를 거칩니다.
	// 퀵바 보관은 OnUnequipped를 안 거쳐서 여기서 안 지우면 조준점이 계속 떠 있다
	HideCrosshairWidget();
	RestoreWorldScale();
}

FBox ACSMagicWandActor::GetUnheldBounds() const
{
	const FBox Bounds = GetComponentsBoundingBox(true);
	const double CurrentScaleSize = GetActorScale3D().Size();
	if (Bounds.IsValid && CachedWorldScale.IsSet() && CurrentScaleSize > UE_SMALL_NUMBER)
	{
		// 장착 배율은 균일 배율이므로 월드 AABB도 Actor 원점을 기준으로 복원할 수 있습니다.
		const double RestoreRatio = CachedWorldScale.GetValue().Size() / CurrentScaleSize;
		const FVector Origin = GetActorLocation();
		return FBox(Origin + (Bounds.Min - Origin) * RestoreRatio,
			Origin + (Bounds.Max - Origin) * RestoreRatio);
	}
	return Bounds;
}

void ACSMagicWandActor::RestoreWorldScale()
{
	if (CachedWorldScale.IsSet())
	{
		SetActorScale3D(CachedWorldScale.GetValue());
		CachedWorldScale.Reset();
	}
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
	if (!bIsEquipped || !IsValid(User) || !IsValid(Muzzle) ||
		MagicProjectileClass == nullptr || GetWorld() == nullptr)
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
	APawn* UserPawn = Cast<APawn>(User);
	if (IsValid(UserPawn))
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(UserPawn->GetController()))
		{
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
	}

	const FVector ViewDirection = ViewRotation.Vector().GetSafeNormal();
	constexpr float AimDistance = 10000.0f;
	FVector AimPoint = ViewLocation + ViewDirection * AimDistance;
	FCollisionQueryParams AimQuery(SCENE_QUERY_STAT(MagicWandAim), false, User);
	AimQuery.AddIgnoredActor(this);
	FHitResult AimHit;
	if (GetWorld()->LineTraceSingleByChannel(AimHit, ViewLocation, AimPoint, ECC_Visibility, AimQuery))
	{
		AimPoint = AimHit.ImpactPoint;
	}

	// 카메라가 가리킨 지점으로 봉 끝에서 수렴시켜 시차에 의한 조준 오차를 없앱니다.
	const FVector SpawnLocation = Muzzle->GetComponentLocation();
	const FVector TravelDirection = (AimPoint - SpawnLocation).GetSafeNormal(UE_SMALL_NUMBER, ViewDirection);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = User;
	SpawnParameters.Instigator = UserPawn;
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
	Projectile->SetEmployeeBoostDuration(EmployeeBoostDuration);

	// Spawn 회전과 초기 속도에 같은 Muzzle -> AimPoint 방향을 사용합니다.
	Projectile->InitializeProjectile(TravelDirection);
	LastPrimaryUseTime = CurrentTime;
	PlayMagicWandGameplaySFX(this, ECSAudioEvent::MagicAttack);

	if (IsValid(ActiveCrosshairWidget))
	{
		ActiveCrosshairWidget->PlayFireReaction();
	}

	return true;
}
