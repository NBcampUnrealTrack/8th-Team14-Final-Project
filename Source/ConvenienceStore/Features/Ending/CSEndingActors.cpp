#include "Features/Ending/CSEndingActors.h"
#include "Features/Ending/CSEndingData.h"
#include "Features/Ending/CSEndingWidget.h"
#include "Features/Inventory/CSQuickbarInventoryComponent.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ACSEndingItemActor::ACSEndingItemActor()
{
	ItemSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ItemSceneRoot"));
	SetRootComponent(ItemSceneRoot);
	ToolMesh->SetupAttachment(ItemSceneRoot);
	// 개별 아이템 BP에 모델을 지정하기 전에도 줍기 테스트가 가능하게 합니다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Mesh.Succeeded()) ToolMesh->SetStaticMesh(Mesh.Object);
	ToolMesh->SetRelativeScale3D(FVector(0.2f));
}

void ACSEndingItemActor::PrepareForPickupPhysics()
{
	if (!IsValid(ToolMesh) || !IsValid(ItemSceneRoot)) return;
	if (GetRootComponent() == ToolMesh)
	{
		Super::PrepareForPickupPhysics();
		// 떨어진 위치에서 편집용 씬 루트를 복원한 뒤 메시를 원래 계층으로 되돌립니다.
		ItemSceneRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		SetRootComponent(ItemSceneRoot);
		ToolMesh->AttachToComponent(ItemSceneRoot, FAttachmentTransformRules::KeepWorldTransform);
		ToolMesh->SetRelativeTransform(StoredMeshRelativeTransform);
		return;
	}
	if (ToolMesh->GetAttachParent() == ItemSceneRoot)
	{
		StoredMeshRelativeTransform = ToolMesh->GetRelativeTransform();
	}
	Super::PrepareForPickupPhysics();
	// 자식 메시의 물리를 끄는 것만으로는 부모 부착이 복구되지 않습니다.
	if (ToolMesh->GetAttachParent() != ItemSceneRoot)
	{
		ToolMesh->AttachToComponent(ItemSceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		ToolMesh->SetRelativeTransform(StoredMeshRelativeTransform);
	}
}

void ACSEndingItemActor::EnableWorldDropPhysics()
{
	if (!IsValid(ToolMesh) || !IsValid(ItemSceneRoot) || !ToolMesh->GetStaticMesh()) return;
	if (GetRootComponent() != ToolMesh)
	{
		StoredMeshRelativeTransform = ToolMesh->GetRelativeTransform();
		// 물리 이동 중에는 툴 메시를 루트로 삼아 추가 메시도 한 물체처럼 따라오게 합니다.
		// 월드 트랜스폼을 유지하므로 BP에서 설정한 각 부품의 배치와 크기는 바뀌지 않습니다.
		ToolMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		SetRootComponent(ToolMesh);
		ItemSceneRoot->AttachToComponent(ToolMesh, FAttachmentTransformRules::KeepWorldTransform);
	}
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		// 추가 장식 메시가 독립된 물리 바디로 떨어지지 않도록 툴 메시만 시뮬레이션합니다.
		if (Primitive != ToolMesh) Primitive->SetSimulatePhysics(false);
	}
	// 공중 배출 시에도 BP의 초기 중력 설정과 무관하게 자연스럽게 낙하합니다.
	ToolMesh->SetEnableGravity(true);
	Super::EnableWorldDropPhysics();
}

void ACSEndingItemActor::OnEquipped(ACSPlayerCharacter* NewOwner)
{
	Super::OnEquipped(NewOwner);
	if (IsValid(NewOwner) && IsValid(IssuingManager)) IssuingManager->NotifyItemPickedUp(this);
}

void ACSEndingItemActor::InitializeItem(const FCSEndingItemDefinition& Definition, ACSEndingManager* Manager)
{
	EndingItemId = Definition.ItemId;
	ToolDisplayName = Definition.DisplayName;
	ToolIcon = Definition.Icon;
	IssuingManager = Manager;
}

ACSEndingManager::ACSEndingManager()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

const UCSEndingData* ACSEndingManager::GetEndingData() const
{
	return IsValid(EndingData) ? EndingData.Get() : GetDefault<UCSEndingData>();
}

FText ACSEndingManager::GetPurchasePrompt() const
{
	const UCSEndingData* Data = GetEndingData();
	if (!Data->IsConfigurationValid()) return NSLOCTEXT("Ending", "InvalidData", "엔딩 데이터 설정을 확인하세요");
	if (bEndingStarted) return NSLOCTEXT("Ending", "Started", "귀환이 시작되었습니다");
	if (IsValid(PendingPickup))
		return NSLOCTEXT("Ending", "PickupFirst", "나온 귀환 재료를 먼저 주워주세요");
	if (!Data->Items.IsValidIndex(NextPurchaseIndex))
		return NSLOCTEXT("Ending", "SoldOut", "모든 귀환 재료를 구매했습니다");
	const FCSEndingItemDefinition& Item = Data->Items[NextPurchaseIndex];
	const ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	return FText::Format(NSLOCTEXT("Ending", "Purchase", "{0} 구매 — {1}G / 보유 {2}G"),
		Item.DisplayName, FText::AsNumber(Item.PriceGold), FText::AsNumber(State ? State->GetCurrentFunds() : 0));
}

void ACSEndingManager::Purchase(ACSPlayerCharacter* Player, const FTransform& SpawnTransform)
{
	const UCSEndingData* Data = GetEndingData();
	ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	if (!IsValid(Player) || !State || bTransactionInProgress || bEndingStarted || !Data->IsConfigurationValid() ||
		!Data->Items.IsValidIndex(NextPurchaseIndex) || IsValid(PendingPickup)) return;
	TGuardValue<bool> TransactionGuard(bTransactionInProgress, true);
	const FCSEndingItemDefinition& Item = Data->Items[NextPurchaseIndex];
	if (!State->CanAfford(Item.PriceGold)) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	ACSEndingItemActor* Spawned = GetWorld()->SpawnActor<ACSEndingItemActor>(Item.ActorClass, SpawnTransform, Params);
	// 생성과 결제가 모두 성공했을 때만 순서를 확정합니다.
	if (!IsValid(Spawned)) return;
	if (!State->TrySpendFunds(Item.PriceGold))
	{
		Spawned->Destroy();
		return;
	}
	Spawned->InitializeItem(Item, this);
	PendingPickup = Spawned;
	++NextPurchaseIndex;
	// 구매로 배출한 실물만 즉시 중력으로 떨어뜨립니다. 전시 복제본에는 적용하지 않습니다.
	Spawned->SetActorEnableCollision(true);
	Spawned->EnableWorldDropPhysics();
}

void ACSEndingManager::NotifyItemPickedUp(ACSEndingItemActor* Item)
{
	// 현재 부착 여부 대신 실제 획득 성공 시점에 배출 대기를 해제합니다.
	if (IsValid(Item) && PendingPickup == Item) PendingPickup = nullptr;
}

FText ACSEndingManager::GetWizardPrompt() const
{
	const UCSEndingData* Data = GetEndingData();
	if (!Data->IsConfigurationValid()) return NSLOCTEXT("Ending", "InvalidData", "엔딩 데이터 설정을 확인하세요");
	if (bEndingStarted) return NSLOCTEXT("Ending", "Started", "귀환이 시작되었습니다");
	if (SubmittedItemIds.Num() == Data->Items.Num())
	{
		const ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
		return FText::Format(NSLOCTEXT("Ending", "Activate", "귀환 실행 — {0}G 제출 / 보유 {1}G"),
			FText::AsNumber(Data->ActivationGold), FText::AsNumber(State ? State->GetCurrentFunds() : 0));
	}
	FString Missing;
	for (const FCSEndingItemDefinition& Item : Data->Items)
	{
		if (!SubmittedItemIds.Contains(Item.ItemId))
		{
			if (!Missing.IsEmpty()) Missing += TEXT(", ");
			Missing += Item.DisplayName.ToString();
		}
	}
	return FText::Format(NSLOCTEXT("Ending", "Submit", "귀환 재료 하나 맡기기 ({0}/4) — 남은 재료: {1}"),
		FText::AsNumber(SubmittedItemIds.Num()), FText::FromString(Missing));
}

void ACSEndingManager::SubmitOrActivate(ACSPlayerCharacter* Player)
{
	const UCSEndingData* Data = GetEndingData();
	if (!IsValid(Player) || bTransactionInProgress || bEndingStarted || !Data->IsConfigurationValid()) return;
	TGuardValue<bool> TransactionGuard(bTransactionInProgress, true);
	if (SubmittedItemIds.Num() < Data->Items.Num())
	{
		const UCSQuickbarInventoryComponent* Inventory = Player->FindComponentByClass<UCSQuickbarInventoryComponent>();
		if (!Inventory) return;
		for (int32 Slot = 0; Slot < Inventory->GetSlotCount(); ++Slot)
		{
			ACSEndingItemActor* Item = Cast<ACSEndingItemActor>(Inventory->GetToolAt(Slot));
			if (!IsValid(Item) || Item->IssuingManager != this || SubmittedItemIds.Contains(Item->EndingItemId)) continue;
			const FName Id = Item->EndingItemId;
			if (!Data->Items.ContainsByPredicate([Id](const FCSEndingItemDefinition& Entry) { return Entry.ItemId == Id; })) continue;
			if (Player->ConsumeQuickbarTool(Item))
			{
				SubmittedItemIds.Add(Id);
				OnSubmissionChanged.Broadcast();
			}
			// 마지막 재료를 맡긴 입력으로 골드까지 지불하지 않습니다.
			return;
		}
		return;
	}

	ACSStoreGameState* State = GetWorld()->GetGameState<ACSStoreGameState>();
	ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(Player->GetController());
	if (!State || !Controller || !Controller->IsLocalController() || !State->CanAfford(Data->ActivationGold)) return;
	TSubclassOf<UUserWidget> WidgetClass = Data->EndingWidgetClass;
	if (!WidgetClass) WidgetClass = UCSEndingWidget::StaticClass();
	UUserWidget* Widget = CreateWidget<UUserWidget>(Controller, WidgetClass);
	if (!Widget) return;
	// 화면과 일시정지가 준비되지 않으면 최종 비용을 차감하지 않습니다.
	if (!UGameplayStatics::SetGamePaused(this, true)) return;
	if (!State->TrySpendFunds(Data->ActivationGold))
	{
		UGameplayStatics::SetGamePaused(this, false);
		return;
	}
	bEndingStarted = true;
	ActiveEndingWidget = Widget;
	State->SetGameClockRunning(false);
	Controller->ClearInteractionContext();
	Controller->SetStoreHUDHidden(this, true);
	Controller->SetStoreTopBarHidden(this, true);
	Widget->AddToViewport(10000);
	Controller->SetGameplayUIInputLock(Widget, true);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget->TakeWidget());
	Controller->SetInputMode(InputMode);
	Controller->bShowMouseCursor = true;
}

ACSEndingInteractionActor::ACSEndingInteractionActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(SceneRoot);
	InteractionBox->SetBoxExtent(FVector(40.0f, 40.0f, 80.0f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSEndingInteractionActor::BeginPlay()
{
	Super::BeginPlay();
	if (IsValid(EndingManager) || !EndingManagerClass) return;

	ACSEndingManager* FoundManager = nullptr;
	for (TActorIterator<ACSEndingManager> It(GetWorld(), EndingManagerClass); It; ++It)
	{
		if (!IsValid(*It)) continue;
		// 관리자가 여러 개면 임의로 골라 서로 다른 진행 상태를 공유하지 않게 합니다.
		if (FoundManager)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: 엔딩 관리자가 여러 개입니다. 레벨 인스턴스의 EndingManager를 직접 지정하세요."), *GetName());
			return;
		}
		FoundManager = *It;
	}
	EndingManager = FoundManager;
	if (!FoundManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 선택한 클래스의 엔딩 관리자를 레벨에 배치하세요."), *GetName());
	}
}

ACSEndingVendorActor::ACSEndingVendorActor()
{
	ItemSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ItemSpawnPoint"));
	// 조준 박스 위치를 조정해도 배출 위치는 함께 이동하지 않도록 분리합니다.
	ItemSpawnPoint->SetupAttachment(SceneRoot);
	ItemSpawnPoint->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
}

void ACSEndingVendorActor::Interact_Implementation(AActor* Interactor)
{
	if (IsValid(EndingManager)) EndingManager->Purchase(Cast<ACSPlayerCharacter>(Interactor), ItemSpawnPoint->GetComponentTransform());
}

FText ACSEndingVendorActor::GetInteractionText_Implementation() const
{
	return IsValid(EndingManager) ? EndingManager->GetPurchasePrompt() : NSLOCTEXT("Ending", "NoManager", "엔딩 관리자를 지정하세요");
}

void ACSEndingWizardActor::Interact_Implementation(AActor* Interactor)
{
	if (IsValid(EndingManager)) EndingManager->SubmitOrActivate(Cast<ACSPlayerCharacter>(Interactor));
}

FText ACSEndingWizardActor::GetInteractionText_Implementation() const
{
	return IsValid(EndingManager) ? EndingManager->GetWizardPrompt() : NSLOCTEXT("Ending", "NoManager", "엔딩 관리자를 지정하세요");
}
