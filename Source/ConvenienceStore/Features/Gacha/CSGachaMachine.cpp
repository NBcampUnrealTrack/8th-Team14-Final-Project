#include "Features/Gacha/CSGachaMachine.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
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
	PrimaryActorTick.bCanEverTick = false;
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	SetRootComponent(InteractionBox);
	InteractionBox->SetBoxExtent(FVector(60, 60, 100));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	DisplayMesh->SetupAttachment(InteractionBox);
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	DisplayMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 2.0f));
	RewardSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("RewardSpawnPoint"));
	RewardSpawnPoint->SetupAttachment(InteractionBox);
	RewardSpawnPoint->SetRelativeLocation(FVector(130.0f, 0.0f, -65.0f));
	ProductActorClass = ACSProductPickupActor::StaticClass();
	static ConstructorHelpers::FClassFinder<ACSProductPickupActor> ProductClass(TEXT("/Game/ConvenienceStore/Blueprints/Items/BP_ProductBase"));
	if (ProductClass.Succeeded()) ProductActorClass = ProductClass.Class;
	ArtifactActorClass = ACSGachaArtifactPickup::StaticClass();
}

void ACSGachaMachine::BeginPlay()
{
	Super::BeginPlay();
	if (RewardPool && RewardPool->ArtifactTable && GetWorld() && GetWorld()->GetGameInstance())
	{
		UGameInstance* Instance = GetWorld()->GetGameInstance();
		if (UCSJournalSubsystem* Journal = Instance->GetSubsystem<UCSJournalSubsystem>()) Journal->RegisterArtifactTable(RewardPool->ArtifactTable);
		if (UCSArtifactSubsystem* Artifacts = Instance->GetSubsystem<UCSArtifactSubsystem>()) Artifacts->RegisterTable(RewardPool->ArtifactTable);
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
	if (const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(PendingReward))
	{
		if (IsValid(DrawingPlayer) && DrawingPlayer->GetHeldActor() == Product) return false;
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
	if (Rewards.IsEmpty() || !Rewards.ContainsByPredicate([this](const FCSGachaPoolRow& Row) { return !IsRewardAvailable(Row); }))
	{
		return FText::Format(NSLOCTEXT("Gacha", "UseMachine", "우물에 {0}G 넣기"), FText::AsNumber(RewardPool->CostGold));
	}
	return NSLOCTEXT("Gacha", "NoRewards", "보상 설정을 확인해 주세요.");
}

void ACSGachaMachine::Interact_Implementation(AActor* Interactor)
{
	ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Interactor);
	if (!IsValid(Player) || !Player->IsLocallyControlled() || !GetWorld() || bDrawing || HasPendingReward() || !RewardPool) return;
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
	OnDrawStarted();
	if (DrawDelaySeconds <= 0.0f) CompleteDraw();
	else GetWorldTimerManager().SetTimer(DrawTimer, this, &ACSGachaMachine::CompleteDraw, DrawDelaySeconds, false);
}

void ACSGachaMachine::CompleteDraw()
{
	bDrawing = false;
	if (!IsValid(DrawingPlayer) || !IsRewardAvailable(SelectedReward) || !GetWorld()) return;
	ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!State || !State->CanAfford(RewardPool->CostGold)) return;
	const FTransform Transform = RewardSpawnPoint->GetComponentTransform();
	AActor* Spawned = nullptr;
	if (SelectedReward.RewardType == ECSGachaRewardType::Product)
	{
		FDataTableRowHandle Handle = SelectedReward.ProductRow;
		if (!Handle.DataTable) Handle.DataTable = RewardPool->ProductTable;
		if (Handle.RowName.IsNone()) Handle.RowName = SelectedReward.RewardId;
		UClass* RowClass = SelectedReward.ActorClass.LoadSynchronous();
		UClass* Class = RowClass && RowClass->IsChildOf(ACSProductPickupActor::StaticClass()) ? RowClass : ProductActorClass.Get();
		ACSProductPickupActor* Product = GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (IsValid(Product) && Product->InitializeProduct(Handle))
		{
			Product->FinishSpawning(Transform);
			Spawned = Product;
		}
		else if (IsValid(Product)) Product->Destroy();
	}
	else if (SelectedReward.RewardType == ECSGachaRewardType::Artifact)
	{
		FDataTableRowHandle Handle = SelectedReward.ArtifactRow;
		if (!Handle.DataTable) Handle.DataTable = RewardPool->ArtifactTable;
		if (Handle.RowName.IsNone()) Handle.RowName = SelectedReward.RewardId;
		ACSGachaArtifactPickup* Artifact = GetWorld()->SpawnActorDeferred<ACSGachaArtifactPickup>(ArtifactActorClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (IsValid(Artifact) && Artifact->InitializeArtifact(Handle, SelectedReward.Quantity))
		{
			Artifact->FinishSpawning(Transform);
			Spawned = Artifact;
		}
		else if (IsValid(Artifact)) Artifact->Destroy();
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
	const FVector Impulse = (GetActorForwardVector() * 0.55f + FVector::UpVector) * LaunchImpulse;
	if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(Spawned)) Product->LaunchFromGacha(Impulse);
	else if (ACSGachaArtifactPickup* Artifact = Cast<ACSGachaArtifactPickup>(Spawned)) Artifact->Launch(Impulse);
	OnRewardDispensed(SelectedReward.RewardType, SelectedReward.RewardId);
	NotifyPlayer(NSLOCTEXT("Gacha", "TakeReward", "보상이 나왔습니다. 획득해 주세요."));
}
