#include "Features/Ending/CSEndingDisplayActor.h"
#include "Features/Ending/CSEndingData.h"
#include "Features/Inventory/CSQuickbarInventoryComponent.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACSEndingDisplayActor::ACSEndingDisplayActor()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ValidMaterial(TEXT("/Game/ConvenienceStore/Asset/ProductAsset/Material/MI_CSPlacementPreview_Valid.MI_CSPlacementPreview_Valid"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> InvalidMaterial(TEXT("/Game/ConvenienceStore/Asset/ProductAsset/Material/MI_CSPlacementPreview_Invalid.MI_CSPlacementPreview_Invalid"));
	ValidPreviewMaterial = ValidMaterial.Object;
	InvalidPreviewMaterial = InvalidMaterial.Object;
	// 선반 전체가 아닌 개별 슬롯의 조준 판정만 사용합니다.
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CompassSlot = CreateDefaultSubobject<UBoxComponent>(TEXT("CompassSlot"));
	ResonanceStoneSlot = CreateDefaultSubobject<UBoxComponent>(TEXT("ResonanceStoneSlot"));
	BoundaryCoreSlot = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundaryCoreSlot"));
	ReturnCoreSlot = CreateDefaultSubobject<UBoxComponent>(TEXT("ReturnCoreSlot"));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UBoxComponent* Slot = GetSlot(Index);
		Slot->SetupAttachment(SceneRoot);
		Slot->SetRelativeLocation(FVector(0.0f, (Index - 1.5f) * 70.0f, 100.0f));
		Slot->SetBoxExtent(FVector(30.0f));
		Slot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Slot->SetCollisionResponseToAllChannels(ECR_Ignore);
		Slot->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}
	DisplayedItems.SetNum(4);
	OriginalScales.Init(FVector::OneVector, 4);
	OriginalAbsoluteScaleFlags.Init(false, 4);
}

UBoxComponent* ACSEndingDisplayActor::GetSlot(int32 Index) const
{
	switch (Index)
	{
	case 0: return CompassSlot;
	case 1: return ResonanceStoneSlot;
	case 2: return BoundaryCoreSlot;
	case 3: return ReturnCoreSlot;
	default: return nullptr;
	}
}

int32 ACSEndingDisplayActor::FindSlot(const UPrimitiveComponent* Component) const
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Component == GetSlot(Index)) return Index;
	}
	return INDEX_NONE;
}

void ACSEndingDisplayActor::BeginPlay()
{
	Super::BeginPlay();
	if (bSubmittedDisplay && IsValid(EndingManager))
	{
		EndingManager->OnSubmissionChanged.AddUniqueDynamic(this, &ACSEndingDisplayActor::RefreshSubmittedDisplay);
		RefreshSubmittedDisplay();
	}
}

void ACSEndingDisplayActor::AttachDisplayItem(ACSEndingItemActor* Item, int32 Index)
{
	Item->PrepareForPickupPhysics();
	// 나침반처럼 여러 메시로 만든 BP도 전체 외형을 유지합니다.
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Item);
	for (UPrimitiveComponent* Primitive : Primitives) Primitive->SetSimulatePhysics(false);
	Item->SetActorEnableCollision(false);
	Item->SetActorHiddenInGame(false);
	Item->AttachToComponent(GetSlot(Index), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	// 슬롯과 전시대의 배율은 상속하지 않고, 기존 크기에 전시 배율만 곱합니다.
	USceneComponent* ItemRoot = Item->GetRootComponent();
	ItemRoot->SetAbsolute(false, false, true);
	FTransform DisplayTransform = EndingManager->GetEndingData()->Items[Index].DisplayTransform;
	DisplayTransform.SetScale3D(OriginalScales[Index] * DisplayTransform.GetScale3D());
	Item->SetActorRelativeTransform(DisplayTransform);
}

void ACSEndingDisplayActor::RefreshSubmittedDisplay()
{
	if (!bSubmittedDisplay || !IsValid(EndingManager) || bUpdatingDisplay) return;
	const UCSEndingData* Data = EndingManager->GetEndingData();
	if (!Data->IsConfigurationValid()) return;
	TGuardValue<bool> Guard(bUpdatingDisplay, true);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FCSEndingItemDefinition& Definition = Data->Items[Index];
		if (!EndingManager->SubmittedItemIds.Contains(Definition.ItemId))
		{
			if (IsValid(DisplayedItems[Index])) DisplayedItems[Index]->Destroy();
			DisplayedItems[Index] = nullptr;
			continue;
		}
		if (IsValid(DisplayedItems[Index])) continue;
		// 전시 복제본은 물리 물건이 아니므로 선반과 겹쳐도 생성할 수 있습니다.
		// 생성 단계에서도 슬롯 배율이 아이템 기본 크기에 섞이지 않게 합니다.
		FTransform SpawnTransform = GetSlot(Index)->GetComponentTransform();
		SpawnTransform.SetScale3D(FVector::OneVector);
		ACSEndingItemActor* Copy = GetWorld()->SpawnActorDeferred<ACSEndingItemActor>(
			Definition.ActorClass, SpawnTransform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Copy) continue;
		Copy->InitializeItem(Definition, EndingManager);
		Copy->SetActorEnableCollision(false);
		Copy->FinishSpawning(SpawnTransform);
		if (!IsValid(Copy)) continue;
		OriginalScales[Index] = Copy->GetActorScale3D();
		OriginalAbsoluteScaleFlags[Index] = Copy->GetRootComponent()->IsUsingAbsoluteScale();
		DisplayedItems[Index] = Copy;
		AttachDisplayItem(Copy, Index);
	}
}

FText ACSEndingDisplayActor::GetInteractionText_Implementation() const
{
	return NSLOCTEXT("Ending", "AimDisplaySlot", "전시 슬롯을 조준하세요");
}

FText ACSEndingDisplayActor::GetSlotPrompt(const ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent) const
{
	const int32 Index = FindSlot(HitComponent);
	if (Index == INDEX_NONE) return GetInteractionText_Implementation();
	if (!IsValid(EndingManager) || !EndingManager->GetEndingData()->IsConfigurationValid())
		return NSLOCTEXT("Ending", "DisplayNoManager", "전시대의 엔딩 관리자 설정을 확인하세요");
	const FCSEndingItemDefinition& Definition = EndingManager->GetEndingData()->Items[Index];
	if (bSubmittedDisplay)
	{
		return FText::Format(EndingManager->SubmittedItemIds.Contains(Definition.ItemId)
			? NSLOCTEXT("Ending", "DisplayedSubmitted", "{0} — 제출 완료 (회수 불가)")
			: NSLOCTEXT("Ending", "DisplayMissing", "{0} — 아직 제출하지 않았습니다"), Definition.DisplayName);
	}
	if (IsValid(DisplayedItems[Index]))
	{
		if (Player && IsValid(Player->GetHeldActor()))
			return NSLOCTEXT("Ending", "OccupiedDisplay", "이미 아이템이 전시되어 있습니다. 빈손으로 꺼내세요");
		const UCSQuickbarInventoryComponent* Inventory = Player ? Player->FindComponentByClass<UCSQuickbarInventoryComponent>() : nullptr;
		bool bHasRoom = false;
		if (Inventory)
			for (int32 Slot = 0; Slot < Inventory->GetSlotCount(); ++Slot) bHasRoom |= !IsValid(Inventory->GetToolAt(Slot));
		if (!bHasRoom) return NSLOCTEXT("Ending", "DisplayInventoryFull", "퀵바에 빈 슬롯이 필요합니다");
		if (IsValid(Player->GetHeldActor()) && !Player->GetHeldActor()->IsA<ACSUsableToolActor>())
			return NSLOCTEXT("Ending", "DisplayHandsFull", "손에 든 물건을 먼저 내려놓으세요");
		return FText::Format(NSLOCTEXT("Ending", "TakeDisplay", "[E] {0} 꺼내기"), Definition.DisplayName);
	}
	const ACSEndingItemActor* Held = Player ? Cast<ACSEndingItemActor>(Player->GetHeldActor()) : nullptr;
	if (Held && Held->EndingItemId == Definition.ItemId && Held->IssuingManager == EndingManager)
		return FText::Format(NSLOCTEXT("Ending", "PlaceDisplayQ", "[Q] {0} 전시하기"), Definition.DisplayName);
	return FText::Format(NSLOCTEXT("Ending", "DisplaySlotName", "{0} 전용 전시 슬롯"), Definition.DisplayName);
}

void ACSEndingDisplayActor::InteractWithSlot(ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent)
{
	if (!IsValid(Player) || IsValid(Player->GetHeldActor())) return;
	const int32 Index = FindSlot(HitComponent);
	if (!IsValid(Player) || Player->IsBuildMode() || bSubmittedDisplay || bUpdatingDisplay || Index == INDEX_NONE ||
		!IsValid(EndingManager) || EndingManager->bEndingStarted || !EndingManager->GetEndingData()->IsConfigurationValid()) return;
	TGuardValue<bool> Guard(bUpdatingDisplay, true);
	ACSEndingItemActor* Item = DisplayedItems[Index];
	if (IsValid(Item))
	{
		// 퀵바 등록에 실패하면 전시 상태를 그대로 유지합니다.
		if (Player->TryPickUp(Item))
		{
			USceneComponent* ItemRoot = Item->GetRootComponent();
			ItemRoot->SetAbsolute(ItemRoot->IsUsingAbsoluteLocation(), ItemRoot->IsUsingAbsoluteRotation(), OriginalAbsoluteScaleFlags[Index]);
			Item->SetActorScale3D(OriginalScales[Index]);
			DisplayedItems[Index] = nullptr;
		}
		return;
	}
	// 빈 슬롯에서 E를 눌러도 배치하지 않습니다.
}

bool ACSEndingDisplayActor::CanPlace(const ACSPlayerCharacter* Player, int32 Index) const
{
	if (!IsValid(Player) || Player->IsBuildMode() || bSubmittedDisplay || bUpdatingDisplay ||
		!DisplayedItems.IsValidIndex(Index) || IsValid(DisplayedItems[Index]) || !IsValid(EndingManager) ||
		EndingManager->bEndingStarted || !EndingManager->GetEndingData()->IsConfigurationValid()) return false;
	const ACSEndingItemActor* Item = Cast<ACSEndingItemActor>(Player->GetHeldActor());
	return IsValid(Item) && Item->IssuingManager == EndingManager &&
		Item->EndingItemId == EndingManager->GetEndingData()->Items[Index].ItemId;
}

void ACSEndingDisplayActor::PlaceInSlot(ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent)
{
	const int32 Index = FindSlot(HitComponent);
	if (!CanPlace(Player, Index)) return;
	TGuardValue<bool> Guard(bUpdatingDisplay, true);
	ACSEndingItemActor* Item = Cast<ACSEndingItemActor>(Player->GetHeldActor());
	if (!IsValid(Item) || Item->IssuingManager != EndingManager ||
		Item->EndingItemId != EndingManager->GetEndingData()->Items[Index].ItemId) return;
	const FVector PreviousScale = Item->GetActorScale3D();
	const bool bPreviousAbsoluteScale = Item->GetRootComponent()->IsUsingAbsoluteScale();
	if (!Player->ReleaseQuickbarTool(Item)) return;
	OriginalScales[Index] = PreviousScale;
	OriginalAbsoluteScaleFlags[Index] = bPreviousAbsoluteScale;
	DisplayedItems[Index] = Item;
	AttachDisplayItem(Item, Index);
	HidePlacementPreview();
}

void ACSEndingDisplayActor::HidePlacementPreview()
{
	if (IsValid(PlacementPreview)) PlacementPreview->SetActorHiddenInGame(true);
}

void ACSEndingDisplayActor::UpdatePlacementPreview(ACSPlayerCharacter* Player, const UPrimitiveComponent* HitComponent)
{
	HidePlacementPreview();
	const int32 Index = FindSlot(HitComponent);
	ACSEndingItemActor* Item = Player ? Cast<ACSEndingItemActor>(Player->GetHeldActor()) : nullptr;
	if (!IsValid(Item) || bSubmittedDisplay || Index == INDEX_NONE || !IsValid(EndingManager)) return;
	const UCSEndingData* Data = EndingManager->GetEndingData();
	if (!Data->IsConfigurationValid()) return;
	// 잘못된 슬롯에서도 손에 든 아이템의 실제 전시 외형을 빨간색으로 보여줍니다.
	const FCSEndingItemDefinition* Definition = Data->Items.FindByPredicate(
		[Item](const FCSEndingItemDefinition& Entry) { return Entry.ItemId == Item->EndingItemId; });
	if (!Definition) return;
	if (!IsValid(PlacementPreview) || PreviewSource.Get() != Item)
	{
		if (IsValid(PlacementPreview)) PlacementPreview->Destroy();
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		PlacementPreview = GetWorld()->SpawnActor<AActor>(Params);
		if (!PlacementPreview) return;
		USceneComponent* Root = NewObject<USceneComponent>(PlacementPreview);
		PlacementPreview->AddInstanceComponent(Root);
		PlacementPreview->SetRootComponent(Root);
		Root->RegisterComponent();
		PlacementPreview->SetActorEnableCollision(false);
		// 게임플레이 BP를 복제하지 않고 정적 메시 외형만 복사하여 부작용을 방지합니다.
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Item);
		for (UStaticMeshComponent* Source : Meshes)
		{
			if (!Source->GetStaticMesh() || !Source->IsVisible() || Source->bHiddenInGame) continue;
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(PlacementPreview);
			PlacementPreview->AddInstanceComponent(Mesh);
			Mesh->SetupAttachment(Root);
			Mesh->SetStaticMesh(Source->GetStaticMesh());
			Mesh->SetRelativeTransform(Source->GetComponentTransform().GetRelativeTransform(Item->GetActorTransform()));
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->RegisterComponent();
		}
		PreviewSource = Item;
	}
	PlacementPreview->AttachToComponent(GetSlot(Index), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	PlacementPreview->GetRootComponent()->SetAbsolute(false, false, true);
	FTransform Transform = Definition->DisplayTransform;
	Transform.SetScale3D(Item->GetActorScale3D() * Transform.GetScale3D());
	PlacementPreview->SetActorRelativeTransform(Transform);
	UMaterialInterface* Material = CanPlace(Player, Index) ? ValidPreviewMaterial.Get() : InvalidPreviewMaterial.Get();
	TInlineComponentArray<UStaticMeshComponent*> Meshes(PlacementPreview);
	for (UStaticMeshComponent* Mesh : Meshes)
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) Mesh->SetMaterial(Slot, Material);
	PlacementPreview->SetActorHiddenInGame(false);
}

void ACSEndingDisplayActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(PlacementPreview)) PlacementPreview->Destroy();
	if (IsValid(EndingManager)) EndingManager->OnSubmissionChanged.RemoveDynamic(this, &ACSEndingDisplayActor::RefreshSubmittedDisplay);
	for (int32 Index = 0; Index < DisplayedItems.Num(); ++Index)
	{
		ACSEndingItemActor* Item = DisplayedItems[Index];
		if (!IsValid(Item)) continue;
		if (bSubmittedDisplay) Item->Destroy();
		else if (EndPlayReason == EEndPlayReason::Destroyed)
		{
			// 가게 전시대를 제거해도 구매한 실물은 월드에 남겨 회수할 수 있게 합니다.
			Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			USceneComponent* ItemRoot = Item->GetRootComponent();
			ItemRoot->SetAbsolute(ItemRoot->IsUsingAbsoluteLocation(), ItemRoot->IsUsingAbsoluteRotation(), OriginalAbsoluteScaleFlags[Index]);
			Item->SetActorScale3D(OriginalScales[Index]);
			Item->SetActorEnableCollision(true);
		}
	}
	Super::EndPlay(EndPlayReason);
}
