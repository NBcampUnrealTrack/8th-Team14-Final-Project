#include "Features/Gacha/CSGachaSeal.h"

#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Features/Gacha/CSGachaArtifactPickup.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "TimerManager.h"

ACSGachaSeal::ACSGachaSeal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	ToolDisplayName = NSLOCTEXT("Gacha", "SealName", "신비한 봉인석");
	SealMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	FragmentMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SealMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ConvenienceStore/Feature/Gacha/M_GachaSeal.M_GachaSeal"));
	ToolMesh->SetStaticMesh(SealMesh);
	ToolMesh->SetRelativeScale3D(FVector(0.45f));
	ToolMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	ToolMesh->SetNotifyRigidBodyCollision(true);
	ToolMesh->SetUseCCD(true);
	ToolMesh->SetLinearDamping(1.5f);
	ToolMesh->SetAngularDamping(2.f);
	ToolMesh->OnComponentHit.AddDynamic(this, &ThisClass::HandleHit);
	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("OpeningFlash"));
	Flash->SetupAttachment(ToolMesh);
	Flash->SetLightColor(FLinearColor(0.2f, 0.8f, 1.f));
	Flash->SetIntensity(0.f);
	Flash->SetAttenuationRadius(300.f);
	Flash->SetCastShadows(false);
}

void ACSGachaSeal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ToolMesh->SetStaticMesh(SealMesh);
	if (SealMaterial) ToolMesh->SetMaterial(0, SealMaterial);
}

void ACSGachaSeal::InitializeReward(const FCSGachaPoolRow& InReward, TSubclassOf<ACSProductPickupActor> InProductClass, TSubclassOf<ACSGachaArtifactPickup> InArtifactClass)
{
	Reward = InReward;
	ProductClass = InProductClass;
	ArtifactClass = InArtifactClass;
	bHasReward = true;
	RecoveryLocation = GetActorLocation();
}

void ACSGachaSeal::Launch(const FVector& Velocity)
{
	// 우물에서 배출될 때는 개봉을 활성화하지 않습니다.
	EnableWorldDropPhysics();
	ToolMesh->SetEnableGravity(true);
	ToolMesh->AddImpulse(Velocity, NAME_None, true);
}

void ACSGachaSeal::Interact_Implementation(AActor* Interactor)
{
	if (bOpened || bOpening || !bHasReward) return;
	Super::Interact_Implementation(Interactor);
}

void ACSGachaSeal::OnEquipped(ACSPlayerCharacter* NewOwner)
{
	Super::OnEquipped(NewOwner);
	bAwaitingImpact = false;
	GetWorldTimerManager().ClearTimer(OpenTimer);
	LastHolder = NewOwner;
	if (IsValid(NewOwner))
	{
		RecoveryLocation = NewOwner->GetActorLocation() + FVector(0, 0, 50);
		if (auto* Controller = Cast<ACSStorePlayerController>(NewOwner->GetController()))
			Controller->ShowGameplayToast(ToolDisplayName, NSLOCTEXT("Gacha", "SealControls", "Q: 내려놓아 개봉 / Q 길게 누른 뒤 놓기: 던져 개봉"));
	}
}

void ACSGachaSeal::OnReleasedToWorld(bool bThrown)
{
	if (!bHasReward || bOpened) return;
	bThrownOpen = bThrown;
	bAwaitingImpact = bThrown;
	ImpactNormal = FVector::UpVector;
	if (!bThrown) GetWorldTimerManager().SetTimer(OpenTimer, this, &ThisClass::OpenSeal, 0.2f, false);
}

void ACSGachaSeal::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bAwaitingImpact || bOpened || OtherActor == LastHolder.Get() || !Hit.bBlockingHit) return;
	bAwaitingImpact = false;
	ImpactNormal = Hit.ImpactNormal;
	// 물리 충돌 콜백이 끝난 뒤 보상을 생성합니다.
	GetWorldTimerManager().SetTimer(OpenTimer, this, &ThisClass::OpenSeal, 0.01f, false);
}

AActor* ACSGachaSeal::SpawnReward(FText& OutFailure)
{
	const FVector Origin = GetActorLocation();
	const FTransform Transform(FRotator::ZeroRotator, Origin + ImpactNormal * 70.f + FVector(0, 0, 30));
	AActor* Spawned = nullptr;
	UStaticMeshComponent* PickupMesh = nullptr;
	OutFailure = NSLOCTEXT("Gacha", "SealSpawnFailed", "보상을 생성하지 못했습니다. 봉인석은 유지됩니다. 보상 액터 설정을 확인해 주세요.");
	const FText DataFailure = NSLOCTEXT("Gacha", "SealDataFailed", "보상 데이터를 불러오지 못했습니다. 봉인석은 유지됩니다. 데이터 테이블 설정을 확인해 주세요.");
	if (Reward.RewardType == ECSGachaRewardType::Product)
	{
		UClass* RowClass = Reward.ActorClass.LoadSynchronous();
		UClass* Class = RowClass && RowClass->IsChildOf(ACSProductPickupActor::StaticClass()) ? RowClass : ProductClass.Get();
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract)) return nullptr;
		auto* Product = GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (IsValid(Product) && Product->InitializeProduct(Reward.ProductRow))
		{
			Product->FinishSpawning(Transform);
			Spawned = Product;
			PickupMesh = Product->GetPickupMesh();
		}
		else if (IsValid(Product)) { OutFailure = DataFailure; Product->Destroy(); }
	}
	else if (Reward.RewardType == ECSGachaRewardType::Artifact && ArtifactClass)
	{
		auto* Artifact = GetWorld()->SpawnActorDeferred<ACSGachaArtifactPickup>(ArtifactClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (IsValid(Artifact) && Artifact->InitializeArtifact(Reward.ArtifactRow, Reward.Quantity))
		{
			Artifact->FinishSpawning(Transform);
			Spawned = Artifact;
			PickupMesh = Artifact->GetPickupMesh();
		}
		else if (IsValid(Artifact)) { OutFailure = DataFailure; Artifact->Destroy(); }
	}
	if (!IsValid(Spawned)) return nullptr;
	// 실제 물리 메시의 단순 충돌만 사용합니다. 장식·상호작용 박스는 공간 판정에서 제외합니다.
	const UBodySetup* Body = IsValid(PickupMesh) && PickupMesh->GetStaticMesh() ? PickupMesh->GetStaticMesh()->GetBodySetup() : nullptr;
	if (!Body || Body->AggGeom.GetElementCount() == 0 || Body->CollisionTraceFlag == CTF_UseComplexAsSimple)
	{
		OutFailure = NSLOCTEXT("Gacha", "SealCollisionInvalid", "보상의 메시 또는 물리 충돌 설정을 확인해 주세요. 봉인석은 유지됩니다.");
		Spawned->Destroy();
		return nullptr;
	}
	const FBox Bounds = Body->AggGeom.CalcAABB(PickupMesh->GetComponentTransform());
	if (!Bounds.IsValid || Bounds.GetExtent().ContainsNaN())
	{
		OutFailure = NSLOCTEXT("Gacha", "SealBoundsInvalid", "보상의 크기 설정을 확인해 주세요. 봉인석은 유지됩니다.");
		Spawned->Destroy();
		return nullptr;
	}
	const FVector Extent = Bounds.GetExtent().ComponentMax(FVector(1.f));
	const FVector CenterOffset = Bounds.GetCenter() - Spawned->GetActorLocation();
	FCollisionQueryParams Query;
	Query.AddIgnoredActor(this);
	Query.AddIgnoredActor(Spawned);
	if (LastHolder.IsValid()) Query.AddIgnoredActor(LastHolder.Get());
	FCollisionResponseParams Responses(PickupMesh->GetCollisionResponseToChannels());
	// 아티팩트 메시는 배출 시 모든 채널을 막고 상품은 플레이어와 겹칠 수 있습니다.
	if (Reward.RewardType == ECSGachaRewardType::Artifact) Responses = FCollisionResponseParams(ECR_Block);
	else Responses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	const float SearchStep = FMath::Max(80.f, 2.f * FMath::Max(Extent.X, Extent.Y) + 20.f);
	bool bFoundGround = false;
	for (int32 Index = 0; Index < 17; ++Index)
	{
		const float Angle = (Index - 1) * PI / 4.f;
		const float Radius = Index == 0 ? 0.f : SearchStep * (Index <= 8 ? 1.f : 2.f);
		const FVector Candidate = Origin + ImpactNormal.GetSafeNormal2D() * (Extent.GetMax() + 10.f) + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
		FHitResult Ground;
		if (!GetWorld()->LineTraceSingleByChannel(Ground, Candidate + FVector(0, 0, 50), Candidate - FVector(0, 0, 1200), ECC_Visibility, Query) || Ground.ImpactNormal.Z < 0.5f) continue;
		bFoundGround = true;
		// 원점이 바닥이나 모서리에 있는 모델도 충돌 상자의 아랫면이 바닥 위에 오도록 보정합니다.
		const FVector Center(Ground.ImpactPoint.X, Ground.ImpactPoint.Y, Ground.ImpactPoint.Z + Extent.Z + 10.f);
		const FVector Location = Center - CenterOffset;
		if (GetWorld()->OverlapBlockingTestByChannel(Center, FQuat::Identity, PickupMesh->GetCollisionObjectType(), FCollisionShape::MakeBox(Extent), Query, Responses)) continue;
		Spawned->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		if (auto* Product = Cast<ACSProductPickupActor>(Spawned)) Product->LaunchFromGacha(FVector(0, 0, 100));
		else if (auto* Artifact = Cast<ACSGachaArtifactPickup>(Spawned)) Artifact->Launch(FVector(0, 0, 100));
		return Spawned;
	}
	OutFailure = bFoundGround
		? NSLOCTEXT("Gacha", "SealNeedsSpace", "보상을 꺼낼 공간이 없습니다. 봉인석을 집어 넓은 곳에서 다시 개봉해 주세요.")
		: NSLOCTEXT("Gacha", "SealNeedsGround", "보상을 놓을 바닥을 찾지 못했습니다. 바닥 가까이에서 다시 개봉해 주세요.");
	UE_LOG(LogTemp, Warning, TEXT("[GachaSeal] Placement failed: Reward=%s Mesh=%s CollisionExtent=%s CenterOffset=%s GroundFound=%d"),
		*Reward.RewardId.ToString(), *GetNameSafe(PickupMesh->GetStaticMesh()), *Extent.ToString(), *CenterOffset.ToString(), bFoundGround);
	Spawned->Destroy();
	return nullptr;
}

void ACSGachaSeal::OpenSeal()
{
	if (bOpened || bOpening || bIsEquipped || !bHasReward) return;
	TGuardValue<bool> Guard(bOpening, true);
	FText Failure;
	if (!SpawnReward(Failure))
	{
		UE_LOG(LogTemp, Warning, TEXT("[GachaSeal] Reward=%s ProductRow=%s ArtifactRow=%s Failure=%s"),
			*Reward.RewardId.ToString(), *Reward.ProductRow.RowName.ToString(), *Reward.ArtifactRow.RowName.ToString(), *Failure.ToString());
		if (LastHolder.IsValid())
			if (auto* Controller = Cast<ACSStorePlayerController>(LastHolder->GetController()))
				Controller->ShowGameplayToast(ToolDisplayName, Failure);
		return;
	}
	bOpened = true;
	bAwaitingImpact = false;
	OpenOrigin = GetActorLocation();
	PrepareForPickupPhysics();
	SetActorEnableCollision(false);
	ToolMesh->SetVisibility(false);
	Flash->SetIntensity(3000.f);
	if (BreakSound) UGameplayStatics::PlaySoundAtLocation(this, BreakSound, OpenOrigin);
	if (bUseDemoFragments && FragmentMesh)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			auto* Fragment = NewObject<UStaticMeshComponent>(this);
			Fragment->SetStaticMesh(FragmentMesh);
			if (SealMaterial) Fragment->SetMaterial(0, SealMaterial);
			Fragment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Fragment->SetCanEverAffectNavigation(false);
			Fragment->RegisterComponent();
			Fragment->SetWorldLocation(OpenOrigin);
			Fragment->SetWorldScale3D(FVector(0.1f));
			Fragments.Add(Fragment);
		}
	}
	SetActorTickEnabled(true);
	OnSealOpened(bThrownOpen);
	OnRewardRevealed.Broadcast(Reward.RewardType, Reward.RewardId);
	SetLifeSpan(FMath::Max(0.2f, BreakDuration) + 0.1f);
}

void ACSGachaSeal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOpened) return;
	BreakElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(BreakElapsed / FMath::Max(0.2f, BreakDuration), 0.f, 1.f);
	Flash->SetIntensity(3000.f * FMath::Square(1.f - Alpha));
	for (int32 Index = 0; Index < Fragments.Num(); ++Index)
	{
		const float Angle = Index * PI / 4.f;
		const float Spread = bThrownOpen ? 110.f : 70.f;
		Fragments[Index]->SetWorldLocation(OpenOrigin + FVector(FMath::Cos(Angle) * Spread * Alpha, FMath::Sin(Angle) * Spread * Alpha, 70.f * FMath::Sin(Alpha * PI)));
		Fragments[Index]->SetWorldRotation(FRotator(Alpha * 180.f, Index * 45.f + Alpha * 120.f, Alpha * 90.f));
		Fragments[Index]->SetWorldScale3D(FVector(0.1f * (1.f - Alpha)));
	}
}

void ACSGachaSeal::FellOutOfWorld(const UDamageType& DamageType)
{
	if (bOpened) { Super::FellOutOfWorld(DamageType); return; }
	// 아직 개봉하지 않은 유료 보상은 맵 밖으로 떨어져도 마지막 안전 위치로 돌려놓습니다.
	GetWorldTimerManager().ClearTimer(OpenTimer);
	bAwaitingImpact = false;
	PrepareForPickupPhysics();
	SetActorLocation(RecoveryLocation, false, nullptr, ETeleportType::TeleportPhysics);
	EnableWorldDropPhysics();
}
