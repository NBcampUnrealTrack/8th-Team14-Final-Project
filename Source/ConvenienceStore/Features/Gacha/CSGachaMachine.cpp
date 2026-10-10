#include "Features/Gacha/CSGachaMachine.h"

#include "Components/BoxComponent.h"
#include "Systems/Audio/CSAudioSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Features/Gacha/CSGachaSeal.h"
#include "Data/CSJournalTypes.h"
#include "Data/CSProductTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Features/Gacha/CSGachaArtifactPickup.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ACSGachaMachine::ACSGachaMachine()
{
	PrimaryActorTick.bCanEverTick = true;
	// 상호작용 영역을 옮겨도 외형과 보상 생성 지점은 함께 움직이지 않도록 분리합니다.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(60, 60, 100));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	DisplayMesh->SetupAttachment(SceneRoot);
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	DisplayMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 2.0f));
	RewardSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("RewardSpawnPoint"));
	RewardSpawnPoint->SetupAttachment(SceneRoot);
	RewardSpawnPoint->SetRelativeLocation(FVector(0.0f, 0.0f, 110.0f));
	MagicCircle = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MagicCircle"));
	MagicCircle->SetupAttachment(RewardSpawnPoint);
	MagicCircle->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	MagicCircle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MagicCircle->SetCastShadow(false);
	MagicCircle->SetCanEverAffectNavigation(false);
	CustomMagicCircle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CustomMagicCircle"));
	// 부모가 담당하는 회전과 배출 시 확장 연출을 교체한 메시에도 그대로 적용합니다.
	CustomMagicCircle->SetupAttachment(MagicCircle);
	CustomMagicCircle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CustomMagicCircle->SetCastShadow(false);
	CustomMagicCircle->SetCanEverAffectNavigation(false);
	Beam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SummoningBeam"));
	Beam->SetupAttachment(RewardSpawnPoint);
	Beam->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);
	Beam->SetVisibility(false);
	GlowMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ConvenienceStore/Feature/Gacha/M_GachaGlow.M_GachaGlow"));
	SealActorClass = ACSGachaSeal::StaticClass();
	ProductActorClass = ACSProductPickupActor::StaticClass();
	static ConstructorHelpers::FClassFinder<ACSProductPickupActor> ProductClass(TEXT("/Game/ConvenienceStore/Blueprints/Items/BP_ProductBase"));
	if (ProductClass.Succeeded()) ProductActorClass = ProductClass.Class;
	ArtifactActorClass = ACSGachaArtifactPickup::StaticClass();
}

void ACSGachaMachine::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	MagicCircle->ClearInstances();
	// 기본 큐브로 가운데가 뚫린 원과 룬을 만들어 모델 없이도 연출을 확인할 수 있게 합니다.
	for (int32 Index = 0; !CircleMesh && Index < 32; ++Index)
	{
		const float Angle = Index * 2.f * PI / 32.f;
		for (const float RadiusScale : {1.f, 0.72f})
		{
			const float Radius = FMath::Max(10.f, CircleRadius) * RadiusScale;
			MagicCircle->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(Angle) + 90.f, 0),
				FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0), FVector(Radius * 0.002f, 0.018f, 0.008f)));
		}
		if (Index % 4 == 0)
			MagicCircle->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(Angle), 0),
				FVector(FMath::Cos(Angle) * CircleRadius * 0.86f, FMath::Sin(Angle) * CircleRadius * 0.86f, 0), FVector(0.075f, 0.025f, 0.01f)));
	}
	MagicCircle->SetVisibility(bUseDemoEffects && bShowMagicCircle && !CircleMesh);
	CustomMagicCircle->SetStaticMesh(CircleMesh);
	CustomMagicCircle->SetVisibility(bUseDemoEffects && bShowMagicCircle && CircleMesh != nullptr);
	if (CircleMesh)
	{
		const FVector MeshExtent = CircleMesh->GetBounds().BoxExtent;
		const float Scale = FMath::Max(10.f, CircleRadius) / FMath::Max(1.f, FMath::Max(MeshExtent.X, MeshExtent.Y));
		FTransform MeshTransform = CircleMeshTransform;
		MeshTransform.SetScale3D(MeshTransform.GetScale3D() * Scale);
		CustomMagicCircle->SetRelativeTransform(MeshTransform);
	}
	UMaterialInterface* CircleSurface = CircleMaterialOverride ? CircleMaterialOverride.Get() : GlowMaterial.Get();
	MagicCircle->SetMaterial(0, CircleSurface);
	CustomMagicCircle->SetMaterial(0, CircleSurface);
	Beam->SetVisibility(false);
	Beam->SetMaterial(0, GlowMaterial);
}

void ACSGachaMachine::BeginPlay()
{
	Super::BeginPlay();
	// 에디터에서 설정한 크기를 기준으로 배출 시 확대 연출을 적용합니다.
	MagicCircleBaseScale = MagicCircle->GetRelativeScale3D();
	CircleMaterial = CircleMesh ? CustomMagicCircle->CreateDynamicMaterialInstance(0) : MagicCircle->CreateDynamicMaterialInstance(0);
	BeamMaterial = Beam->CreateDynamicMaterialInstance(0);
	if (RewardPool && RewardPool->ArtifactTable && GetWorld() && GetWorld()->GetGameInstance())
	{
		UGameInstance* Instance = GetWorld()->GetGameInstance();
		if (UCSJournalSubsystem* Journal = Instance->GetSubsystem<UCSJournalSubsystem>()) Journal->RegisterArtifactTable(RewardPool->ArtifactTable);
		if (UCSArtifactSubsystem* Artifacts = Instance->GetSubsystem<UCSArtifactSubsystem>()) Artifacts->RegisterTable(RewardPool->ArtifactTable);
	}
}

void ACSGachaMachine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	EffectElapsed += DeltaSeconds;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(EffectElapsed * 1.8f);
	const FName GlowParameter = CircleMaterialOverride ? CircleGlowParameterName : FName(TEXT("Glow"));
	if (CircleMaterial && !GlowParameter.IsNone()) CircleMaterial->SetScalarParameterValue(GlowParameter, bDrawing ? 4.f : 0.65f + Pulse * 0.3f);
	DispensePulseRemaining = FMath::Max(0.f, DispensePulseRemaining - DeltaSeconds);
	const float DispenseAlpha = DispensePulseRemaining / 0.4f;
	const float DispenseScale = DispenseAlpha > 0.f ? 1.f + (1.f - DispenseAlpha) * 0.25f : 1.f;
	MagicCircle->SetRelativeScale3D(MagicCircleBaseScale * DispenseScale);
	if (DispenseAlpha > 0.f && CircleMaterial && !GlowParameter.IsNone()) CircleMaterial->SetScalarParameterValue(GlowParameter, 1.f + DispenseAlpha * 5.f);
	if (bDrawing) MagicCircle->AddLocalRotation(FRotator(0, DeltaSeconds * 70.f, 0));
	Beam->SetVisibility(bUseDemoEffects && bShowBeam && bBeamActive);
	if (bBeamActive)
	{
		const float Alpha = FMath::Clamp(EffectElapsed / FMath::Max(0.01f, DrawDelaySeconds), 0.f, 1.f);
		const float Width = FMath::Max(0.01f, FMath::Sin(Alpha * PI)) * 0.22f;
		const float Length = BeamHeight * FMath::Clamp(Alpha / 0.25f, 0.001f, 1.f);
		Beam->SetRelativeLocation(FVector(0, 0, BeamHeight - Length * 0.5f));
		Beam->SetRelativeScale3D(FVector(Width, Width, Length / 100.f));
		if (BeamMaterial) BeamMaterial->SetScalarParameterValue(TEXT("Glow"), 8.f);
	}
}

TArray<FCSGachaPoolRow> ACSGachaMachine::BuildRewards() const
{
	TArray<FCSGachaPoolRow> Result;
	if (!RewardPool || !RewardPool->GachaTable || RewardPool->GachaTable->GetRowStruct() != FCSGachaPoolRow::StaticStruct()) return Result;
	for (const FName RowName : RewardPool->GachaTable->GetRowNames())
	{
		if (const FCSGachaPoolRow* Row = RewardPool->GachaTable->FindRow<FCSGachaPoolRow>(RowName, TEXT("Gacha draw")))
		{
			// 이전 테이블에 엔딩 행이 남아 있어도 구매 전용 아이템은 추첨하지 않습니다.
			if (Row->bEnabled && Row->Weight > 0 &&
				(Row->RewardType == ECSGachaRewardType::Product || Row->RewardType == ECSGachaRewardType::Artifact)) Result.Add(*Row);
		}
	}
	return Result;
}

void ACSGachaMachine::NotifyPlayer(const FText& Message) const
{
	if (IsValid(DrawingPlayer))
	{
		if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(DrawingPlayer->GetController()))
		{
			Controller->ShowGameplayToast(NSLOCTEXT("Gacha", "MachineTitle", "신비한 우물"), Message);
		}
	}
}

bool ACSGachaMachine::HasPendingReward() const
{
	if (!IsValid(PendingReward)) return false;
	if (const ACSGachaSeal* Seal = Cast<ACSGachaSeal>(PendingReward))
	{
		if (Seal->IsOpened() || (IsValid(DrawingPlayer) && DrawingPlayer->GetHeldActor() == Seal)) return false;
	}
	return FVector::DistSquared(PendingReward->GetActorLocation(), RewardSpawnPoint->GetComponentLocation()) < FMath::Square(150.0f);
}

bool ACSGachaMachine::IsRewardAvailable(const FCSGachaPoolRow& Reward) const
{
	if (!Reward.bEnabled || Reward.Weight <= 0 || Reward.Quantity < 1 || Reward.RewardId.IsNone() || !RewardPool || !GetWorld()) return false;
	if (Reward.RewardType == ECSGachaRewardType::Product)
	{
		if (Reward.Quantity != 1) return false;
		FDataTableRowHandle Handle = Reward.ProductRow;
		if (!Handle.DataTable) Handle.DataTable = RewardPool->ProductTable;
		if (Handle.RowName.IsNone()) Handle.RowName = Reward.RewardId;
		return ProductActorClass && Handle.GetRow<FCSProductDataRow>(TEXT("Gacha product")) != nullptr;
	}
	if (Reward.RewardType == ECSGachaRewardType::Artifact)
	{
		if (Reward.ArtifactRow.DataTable && Reward.ArtifactRow.DataTable != RewardPool->ArtifactTable) return false;
		const UGameInstance* Instance = GetWorld()->GetGameInstance();
		const UCSArtifactSubsystem* Artifacts = Instance ? Instance->GetSubsystem<UCSArtifactSubsystem>() : nullptr;
		FCSJournalArtifactDataRow Definition;
		return ArtifactActorClass && Artifacts && Artifacts->GetDefinition(Reward.ArtifactRow.RowName.IsNone() ? Reward.RewardId : Reward.ArtifactRow.RowName, Definition);
	}
	return false;
}

FText ACSGachaMachine::GetInteractionText_Implementation() const
{
	if (bDrawing) return NSLOCTEXT("Gacha", "Drawing", "뽑는 중입니다.");
	if (HasPendingReward()) return NSLOCTEXT("Gacha", "TakePending", "나온 보상을 먼저 가져가 주세요.");
	const TArray<FCSGachaPoolRow> Rewards = BuildRewards();
	if (SealActorClass && !Rewards.IsEmpty() && !Rewards.ContainsByPredicate([this](const FCSGachaPoolRow& Row) { return !IsRewardAvailable(Row); }))
	{
		return FText::Format(NSLOCTEXT("Gacha", "UseMachine", "우물에 {0}G 넣기"), FText::AsNumber(RewardPool->CostGold));
	}
	return NSLOCTEXT("Gacha", "NoRewards", "보상 설정을 확인해 주세요.");
}

void ACSGachaMachine::Interact_Implementation(AActor* Interactor)
{
	ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Interactor);
	if (!IsValid(Player) || !Player->IsLocallyControlled() || !GetWorld() || bDrawing || HasPendingReward() || !RewardPool || !SealActorClass) return;
	DrawingPlayer = Player;
	ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!State || RewardPool->CostGold < 0) return;
	if (!State->CanAfford(RewardPool->CostGold))
	{
		NotifyPlayer(NSLOCTEXT("Gacha", "InsufficientFunds", "골드가 부족합니다."));
		return;
	}
	const TArray<FCSGachaPoolRow> Rewards = BuildRewards();
	int64 TotalWeight = 0;
	for (const FCSGachaPoolRow& Reward : Rewards)
	{
		// 잘못 설정된 행을 제외하고 재정규화하면 기획 확률이 변하므로 추첨 전체를 중단합니다.
		if (!IsRewardAvailable(Reward))
		{
			NotifyPlayer(NSLOCTEXT("Gacha", "InvalidPool", "보상 설정을 확인해 주세요."));
			return;
		}
		TotalWeight += Reward.Weight;
	}
	if (TotalWeight <= 0 || TotalWeight > MAX_int32)
	{
		NotifyPlayer(NSLOCTEXT("Gacha", "EmptyPool", "준비된 보상이 없습니다."));
		return;
	}
	int32 Draw = FMath::RandRange(1, static_cast<int32>(TotalWeight));
	for (const FCSGachaPoolRow& Reward : Rewards)
	{
		Draw -= Reward.Weight;
		if (Draw <= 0) { SelectedReward = Reward; break; }
	}
	bDrawing = true;
	bBeamActive = true;
	EffectElapsed = 0.f;
	// 실제 빛 연출이 시작되는 단일 지점에서만 재생합니다.
	Beam->SetVisibility(bUseDemoEffects && bShowBeam);
	OnDrawStarted();
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::GachaWellBlessing);
	if (DrawDelaySeconds <= 0.0f) FinishBeam();
	else GetWorldTimerManager().SetTimer(DrawTimer, this, &ACSGachaMachine::FinishBeam, DrawDelaySeconds, false);
}

void ACSGachaMachine::FinishBeam()
{
	bBeamActive = false;
	Beam->SetVisibility(false);
	OnBeamFinished();
	GetWorldTimerManager().SetTimer(DrawTimer, this, &ACSGachaMachine::CompleteDraw, FMath::Max(0.01f, BeamToSealDelay), false);
}

void ACSGachaMachine::CompleteDraw()
{
	bDrawing = false;
	if (!IsValid(DrawingPlayer) || !IsRewardAvailable(SelectedReward) || !GetWorld()) return;
	ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!State || !State->CanAfford(RewardPool->CostGold)) return;
	FCSGachaPoolRow SealedReward = SelectedReward;
	if (!SealedReward.ProductRow.DataTable) SealedReward.ProductRow.DataTable = RewardPool->ProductTable;
	if (SealedReward.ProductRow.RowName.IsNone()) SealedReward.ProductRow.RowName = SealedReward.RewardId;
	if (!SealedReward.ArtifactRow.DataTable) SealedReward.ArtifactRow.DataTable = RewardPool->ArtifactTable;
	if (SealedReward.ArtifactRow.RowName.IsNone()) SealedReward.ArtifactRow.RowName = SealedReward.RewardId;
	const FTransform Transform(FRotator::ZeroRotator, RewardSpawnPoint->GetComponentLocation());
	ACSGachaSeal* Spawned = GetWorld()->SpawnActorDeferred<ACSGachaSeal>(SealActorClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (IsValid(Spawned))
	{
		Spawned->InitializeReward(SealedReward, ProductActorClass, ArtifactActorClass);
		Spawned->OnRewardRevealed.AddDynamic(this, &ThisClass::HandleRewardRevealed);
		Spawned->FinishSpawning(Transform);
	}
	// 액터 생성이 끝난 뒤 결제하여 생성 실패에 따른 골드 손실을 방지합니다.
	if (!IsValid(Spawned))
	{
		NotifyPlayer(NSLOCTEXT("Gacha", "SpawnFailed", "보상을 만들지 못해 뽑기가 취소되었습니다."));
		return;
	}
	if (!State->TrySpendFunds(RewardPool->CostGold))
	{
		Spawned->Destroy();
		NotifyPlayer(NSLOCTEXT("Gacha", "PaymentFailed", "결제에 실패해 뽑기가 취소되었습니다."));
		return;
	}
	PendingReward = Spawned;
	DispensePulseRemaining = 0.4f;
	const FVector Impulse = (GetActorForwardVector() * 0.55f + FVector::UpVector) * LaunchImpulse;
	Spawned->Launch(Impulse);
	if (UCSAudioSubsystem* Audio = UCSAudioSubsystem::Get(this)) Audio->PlaySFX(ECSAudioEvent::GachaCapsuleSpawn);
	OnSealDispensed();
	NotifyPlayer(NSLOCTEXT("Gacha", "TakeSeal", "봉인석이 나왔습니다. 집어서 Q로 개봉해 주세요."));
}

void ACSGachaMachine::HandleRewardRevealed(ECSGachaRewardType Type, FName RewardId)
{
	OnRewardDispensed(Type, RewardId);
}
