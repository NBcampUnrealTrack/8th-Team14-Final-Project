#include "Features/Crafting/CSCraftStationActor.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Data/CSCraftFailureVisualRegistry.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Features/Delivery/CSDeliveryCrateActor.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Items/CSProductPlacementPreviewActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Storage/CSFreshnessSafeZoneActor.h"
#include "Features/Trash/CSTrashActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "Systems/SaveLoad/CSSaveRecordUtils.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "TimerManager.h"
#include "UI/Crafting/CSCraftAutoPanelWidget.h"
#include "UI/Crafting/CSCraftCloseupWidget.h"
#include "UI/Crafting/CSCraftIngredientPanelWidget.h"
#include "UI/Crafting/CSCraftRecipeBookWidget.h"
#include "UI/Crafting/CSCraftStationInfoWidget.h"
#include "UI/Crafting/CSCraftSummonListWidget.h"

namespace
{
	const FName MsgTooManyKinds(TEXT("TooManyKinds"));
	const FName MsgSlotFull(TEXT("SlotFull"));
	const FName MsgIngredientLocked(TEXT("IngredientLocked"));
	const FName MsgExpiredIngredient(TEXT("ExpiredIngredient"));

	// 모형은 E 조준용 Visibility만 막는다
	void SetupModelComponent(UStaticMeshComponent* Model, USceneComponent* Parent)
	{
		Model->SetupAttachment(Parent);
		Model->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Model->SetCollisionResponseToAllChannels(ECR_Ignore);
		Model->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Model->SetGenerateOverlapEvents(false);
		Model->SetCanEverAffectNavigation(false);
	}

	FTransform LocationAndRotationOnly(const USceneComponent* Component)
	{
		return FTransform(Component->GetComponentQuat(), Component->GetComponentLocation());
	}

	// 판정은 ACSProductPickupActor::IsSellable과 같다
	int32 CountUsableCrateUnits(const ACSDeliveryCrateActor* Crate)
	{
		const UWorld* World = Crate->GetWorld();
		const ACSStoreGameState* GameState = World ? World->GetGameState<ACSStoreGameState>() : nullptr;
		if (!IsValid(GameState))
		{
			return Crate->GetRemainingQuantity();
		}

		const FCSProductDataRow* ProductData =
			Crate->GetProductRow().GetRow<FCSProductDataRow>(TEXT("CountUsableCrateUnits"));
		const bool bChecksFreshness = ProductData != nullptr &&
			(ProductData->ShelfType == ECSProductShelfType::ChilledShelf ||
				ProductData->ShelfType == ECSProductShelfType::FreezerShelf);
		auto IsUsable = [GameState, bChecksFreshness](const FCSProductStateRecord& State)
		{
			const bool bExpired = State.bExpirationInitialized && State.ExpirationBusinessDay > 0 &&
				GameState->CurrentBusinessDay >= State.ExpirationBusinessDay;
			return !bExpired && (!bChecksFreshness || !State.bFreshnessInitialized || State.Freshness > 0);
		};

		// 포장 상자의 낱개별 상태는 세이브 기록으로만 밖에서 읽을 수 있다
		if (Crate->IsPackingCrate())
		{
			FCSCrateRecord Record;
			Crate->CaptureSaveRecord(Record);
			int32 Count = 0;
			for (const FCSProductStateRecord& State : Record.PackedProductStates)
			{
				Count += IsUsable(State) ? 1 : 0;
			}
			return Count;
		}
		const FCSProductStateRecord State = CSSaveRecordUtils::ToRecord(Crate->GetProductRuntimeState(), Crate->GetCurrentFreshness());
		return IsUsable(State) ? Crate->GetRemainingQuantity() : 0;
	}
}

ACSCraftStationActor::ACSCraftStationActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SceneRoot);
	BodyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	IngredientDropPoint = CreateDefaultSubobject<USceneComponent>(TEXT("IngredientDropPoint"));
	IngredientDropPoint->SetupAttachment(SceneRoot);
	// 솥 물약 면(StirPivot 높이 199) 조금 위
	IngredientDropPoint->SetRelativeLocation(FVector(-28.6f, 32.45f, 215.0f));

	ResultModel1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ResultModel1"));
	ResultModel2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ResultModel2"));
	SetupModelComponent(ResultModel1, SceneRoot);
	SetupModelComponent(ResultModel2, SceneRoot);

	CloseupCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CloseupCamera"));
	CloseupCamera->SetupAttachment(SceneRoot);
	CloseupCamera->SetRelativeLocationAndRotation(FVector(-120.0f, 0.0f, 170.0f), FRotator(-50.0f, 0.0f, 0.0f));
	CloseupCamera->SetAutoActivate(false);

	InfoWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("InfoWidgetComponent"));
	InfoWidgetComponent->SetupAttachment(SceneRoot);
	InfoWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	InfoWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	InfoWidgetComponent->SetDrawAtDesiredSize(false);
	InfoWidgetComponent->SetDrawSize(FVector2D(400.0f, 200.0f));
	// 월드 위젯은 1픽셀이 1cm라 그대로 두면 4m짜리 판이 된다. 글자 해상도는 유지하고 크기만 줄인다
	InfoWidgetComponent->SetRelativeScale3D(FVector(0.25f));
	InfoWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InfoWidgetComponent->SetTwoSided(true);

	SummonCircleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SummonCircleMesh"));
	SummonCircleMesh->SetupAttachment(SceneRoot);
	SummonCircleMesh->SetRelativeLocation(FVector(0.0f, 120.0f, 0.0f));
	// E 조준을 받고, 물리로 떨어지는 소환물의 바닥도 된다
	SummonCircleMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	SummonCircleMesh->SetCanEverAffectNavigation(false);

	SummonArea = CreateDefaultSubobject<UBoxComponent>(TEXT("SummonArea"));
	SummonArea->SetupAttachment(SceneRoot);
	SummonArea->SetRelativeLocation(FVector(0.0f, 120.0f, 40.0f));
	SummonArea->SetBoxExtent(FVector(50.0f, 50.0f, 40.0f));
	// 점유는 물건 위치로 판정한다. 겹침으로 보면 충돌 설정이 제각각인 상품·상자를 놓친다
	SummonArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACSCraftStationActor::BeginPlay()
{
	Super::BeginPlay();

	ResultModelBaseTransforms.SetNum(ResultSlotCount);
	for (int32 Index = 0; Index < ResultSlotCount; ++Index)
	{
		ResultModelBaseTransforms[Index] = GetResultModel(Index)->GetRelativeTransform();
	}

	if (!ProductActorClass || !TrashActorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: ProductActorClass / TrashActorClass를 지정해 주세요. 완성할 수 없습니다."), *GetName());
	}

	// 솥 위 월드 정보 창은 쓰지 않는다. 같은 정보는 클로즈업 화면 위쪽에 나온다
	if (IsValid(InfoWidgetComponent))
	{
		InfoWidgetComponent->SetVisibility(false);
	}

	RefreshModels();
	RefreshInfoWidget();
}

void ACSCraftStationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 열린 창을 안 닫으면 입력 잠금 목록에 죽은 위젯이 남아 입력이 계속 막힌다
	CloseSummonList();
	CloseAutoPanel();
	CloseRecipeBook();
	CloseIngredientPanel();
	// 클로즈업 중에 끝나면 플레이어 입력, 시점, HUD가 잠긴 채 남는다
	if (bCloseupActive)
	{
		ExitCloseup(true);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BubbleTimerHandle);
	}
	RestorePawnVisibility();

	Super::EndPlay(EndPlayReason);
}

// =====================================================================
// 조회 도우미
// =====================================================================

UStaticMeshComponent* ACSCraftStationActor::GetResultModel(int32 SlotIndex) const
{
	switch (SlotIndex)
	{
	case 0: return ResultModel1;
	case 1: return ResultModel2;
	default: return nullptr;
	}
}

UCSCraftingSubsystem* ACSCraftStationActor::GetCraftingSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UCSCraftingSubsystem>() : nullptr;
}

ACSStorePlayerController* ACSCraftStationActor::GetLocalStoreController() const
{
	const UWorld* World = GetWorld();
	return World ? Cast<ACSStorePlayerController>(World->GetFirstPlayerController()) : nullptr;
}

UCSProductVisualRegistry* ACSCraftStationActor::ResolveVisualRegistry(const ACSProductPickupActor* Product) const
{
	if (IsValid(VisualRegistry))
	{
		return VisualRegistry;
	}
	return IsValid(Product) ? Product->GetVisualRegistry() : nullptr;
}

FText ACSCraftStationActor::GetMessage(FName MessageRowName) const
{
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	return IsValid(Crafting) ? Crafting->GetMessageText(MessageRowName) : FText::FromName(MessageRowName);
}

void ACSCraftStationActor::ShowMessage(const FText& Message, bool bWarning) const
{
	// 공용 배너는 경고 톤이라 클로즈업 안의 결과 안내까지 경고처럼 보인다
	if (bCloseupActive && IsValid(ActiveCloseupWidget))
	{
		ActiveCloseupWidget->ShowMessage(Message, bWarning);
		return;
	}
	if (ACSStorePlayerController* Controller = GetLocalStoreController())
	{
		Controller->ShowNoticeBanner(Message);
	}
}

void ACSCraftStationActor::ShowBanner(FName MessageRowName, bool bWarning) const
{
	ShowMessage(GetMessage(MessageRowName), bWarning);
}

bool ACSCraftStationActor::IsBeforeCraftStart() const
{
	return (CraftStep == ECSCraftStep::Idle || CraftStep == ECSCraftStep::Heat) && !bIngredientsLocked;
}

bool ACSCraftStationActor::IsAnyCloseupPanelOpen() const
{
	return IsValid(ActiveAutoPanelWidget) || IsValid(ActiveRecipeBookWidget) || IsValid(ActiveIngredientPanelWidget);
}

ACSCraftStationActor::EAimTarget ACSCraftStationActor::TraceAimTarget(
	const APlayerController* PlayerController,
	int32& OutSlotIndex) const
{
	OutSlotIndex = INDEX_NONE;
	const UWorld* World = GetWorld();
	if (!IsValid(PlayerController) || World == nullptr)
	{
		return EAimTarget::None;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CSCraftStationAim), false);
	QueryParams.AddIgnoredActor(PlayerController->GetPawn());

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(
		Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * InteractTraceDistance, ECC_Visibility, QueryParams) ||
		Hit.GetActor() != this)
	{
		return EAimTarget::None;
	}

	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (HitComponent != nullptr && HitComponent == SummonCircleMesh)
	{
		return EAimTarget::SummonCircle;
	}
	for (int32 Index = 0; Index < ResultSlotCount; ++Index)
	{
		if (HitComponent == GetResultModel(Index) && IsValid(ResultSlots[Index]))
		{
			OutSlotIndex = Index;
			return EAimTarget::Result;
		}
	}
	return EAimTarget::Body;
}

TMap<FName, int32> ACSCraftStationActor::CountIngredients() const
{
	TMap<FName, int32> Counts;
	for (const ACSProductPickupActor* Product : IngredientSlots)
	{
		if (IsValid(Product))
		{
			++Counts.FindOrAdd(Product->GetProductId());
		}
	}
	return Counts;
}

bool ACSCraftStationActor::HasResultProduct() const
{
	for (const ACSProductPickupActor* Product : ResultSlots)
	{
		if (IsValid(Product))
		{
			return true;
		}
	}

	// 실패 쓰레기는 결과 슬롯에 들어가지 않아서, 치우기 전에 또 만들면 같은 자리에 겹쳐 쌓인다
	const ACSTrashActor* Trash = LastFailureTrash.Get();
	if (!IsValid(Trash) || Trash->GetAttachParentActor() != nullptr || !IsValid(ResultModel1))
	{
		return false;
	}
	// 물리로 떨어진 메시는 액터 루트에서 떨어져 움직여서 메시 범위 중심을 본다
	constexpr float ResultSpotRadius = 60.0f;
	const FBox Bounds = Trash->GetComponentsBoundingBox(true);
	const FVector TrashCenter = Bounds.IsValid ? Bounds.GetCenter() : Trash->GetActorLocation();
	return FVector::DistSquared2D(TrashCenter, ResultModel1->GetComponentLocation()) <= FMath::Square(ResultSpotRadius);
}

FName ACSCraftStationActor::GetPlacementRejectReason(const ACSProductPickupActor* Product) const
{
	if (bIngredientsLocked)
	{
		return MsgIngredientLocked;
	}
	// 손님이 안 사는 상품은 재료로도 못 쓰게 한다
	if (!Product->IsSellable())
	{
		return MsgExpiredIngredient;
	}

	TSet<FName> Kinds;
	bool bHasEmptySlot = false;
	for (const ACSProductPickupActor* Stored : IngredientSlots)
	{
		if (IsValid(Stored))
		{
			Kinds.Add(Stored->GetProductId());
		}
		else
		{
			bHasEmptySlot = true;
		}
	}
	if (!bHasEmptySlot)
	{
		return MsgSlotFull;
	}
	if (!Kinds.Contains(Product->GetProductId()) && Kinds.Num() >= 2)
	{
		return MsgTooManyKinds;
	}
	return NAME_None;
}

// =====================================================================
// 재료 투입 (ICSShelfPlaceable)
// =====================================================================

bool ACSCraftStationActor::CanPlaceItem_Implementation(AActor* ItemActor) const
{
	const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ItemActor);
	return IsValid(Product) && !Product->GetProductId().IsNone() && GetPlacementRejectReason(Product).IsNone();
}

bool ACSCraftStationActor::GetPlacementPreview_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	if (!Execute_CanPlaceItem(this, ItemActor))
	{
		return false;
	}
	// 거절 사유가 없으면 빈 슬롯이 있다
	OutSlotWorldTransform = LocationAndRotationOnly(IngredientDropPoint);
	return true;
}

ECSShelfPlacementPreviewState ACSCraftStationActor::GetPlacementPreviewState_Implementation(
	AActor* ItemActor,
	FVector PlacementHint,
	FTransform& OutSlotWorldTransform) const
{
	// Q는 미리보기 상태와 상관없이 TryPlaceItem까지 오므로 거절 안내는 TryPlaceItem에서 띄운다
	return Execute_GetPlacementPreview(this, ItemActor, PlacementHint, OutSlotWorldTransform)
		? ECSShelfPlacementPreviewState::Valid
		: ECSShelfPlacementPreviewState::Invalid;
}

bool ACSCraftStationActor::TryPlaceItem_Implementation(AActor* ItemActor, FVector PlacementHint)
{
	ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ItemActor);
	if (!IsValid(Product) || Product->GetProductId().IsNone())
	{
		return false;
	}

	const FName RejectReason = GetPlacementRejectReason(Product);
	if (!RejectReason.IsNone())
	{
		ShowBanner(RejectReason);
		return false;
	}

	for (int32 Index = 0; Index < IngredientSlotCount; ++Index)
	{
		if (!IsValid(IngredientSlots[Index]))
		{
			IngredientSlots[Index] = Product;
			StoreProductHidden(Product);
			break;
		}
	}
	RefreshInfoWidget();
	OnIngredientAdded(Product->GetProductId());
	if (UCSCraftingSubsystem* Crafting = GetCraftingSubsystem())
	{
		int32 IngredientCount = 0;
		for (const TObjectPtr<ACSProductPickupActor>& Ingredient : IngredientSlots)
		{
			IngredientCount += IsValid(Ingredient) ? 1 : 0;
		}
		Crafting->NotifyIngredientAdded(IngredientCount);
	}
	return true;
}

void ACSCraftStationActor::RemovePlacedItem_Implementation(AActor* ItemActor)
{
	ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ItemActor);
	if (!IsValid(Product))
	{
		return;
	}

	for (int32 Index = 0; Index < IngredientSlotCount; ++Index)
	{
		if (IngredientSlots[Index] == Product)
		{
			IngredientSlots[Index] = nullptr;
			RestoreProductVisible(Product, IngredientDropPoint);
		}
	}
	for (int32 Index = 0; Index < ResultSlotCount; ++Index)
	{
		if (ResultSlots[Index] == Product)
		{
			ResultSlots[Index] = nullptr;
			RestoreProductVisible(Product, GetResultModel(Index));
		}
	}
	RefreshModels();
	RefreshInfoWidget();
}

bool ACSCraftStationActor::GetItemSlotLevel_Implementation(AActor* ItemActor, ECSShelfSlotLevel& OutSlotLevel) const
{
	return false;
}

FText ACSCraftStationActor::GetPlacementPromptText() const
{
	return GetMessage(TEXT("PlacePrompt"));
}

FText ACSCraftStationActor::GetPlacementFailurePromptText(const AActor* ItemActor) const
{
	const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(ItemActor);
	if (!IsValid(Product))
	{
		return FText::GetEmpty();
	}
	const FName RejectReason = GetPlacementRejectReason(Product);
	return RejectReason.IsNone() ? FText::GetEmpty() : GetMessage(RejectReason);
}

void ACSCraftStationActor::StoreProductHidden(ACSProductPickupActor* Product)
{
	// 손에 붙은 채 숨기면 플레이어를 따라다닌다
	if (Product->GetAttachParentActor() != nullptr)
	{
		Product->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	Product->PrepareForPickupPhysics();
	Product->SetActorHiddenInGame(true);
	Product->SetActorEnableCollision(false);
	// 재고 집계와 직원 업무 대상에서 뺀다
	Product->SetExcludedFromStockCount(true);
}

void ACSCraftStationActor::RestoreProductVisible(ACSProductPickupActor* Product, const USceneComponent* AtModel)
{
	if (IsValid(AtModel))
	{
		Product->SetActorLocationAndRotation(AtModel->GetComponentLocation(), AtModel->GetComponentQuat());
	}
	Product->SetExcludedFromStockCount(false);
	Product->SetActorEnableCollision(true);
	Product->SetActorHiddenInGame(false);
}

TArray<ACSProductPickupActor*> ACSCraftStationActor::AddTagsToResultProducts(const TArray<FName>& NewTags)
{
	TArray<ACSProductPickupActor*> Products;
	for (const TObjectPtr<ACSProductPickupActor>& Product : ResultSlots)
	{
		if (IsValid(Product))
		{
			for (const FName& Tag : NewTags)
			{
				Product->Tags.AddUnique(Tag);
			}
			Products.Add(Product);
		}
	}
	return Products;
}

void ACSCraftStationActor::CloseAllPanelsAndCloseup()
{
	CloseSummonList();
	ExitCloseup(true);
}

void ACSCraftStationActor::SetCloseupGuidesHidden(bool bShouldHide)
{
	bCloseupGuidesHidden = bShouldHide;
	if (IsValid(ActiveCloseupWidget))
	{
		ActiveCloseupWidget->SetGuidesHidden(bShouldHide);
	}
}

FCSCraftStationSaveRecord ACSCraftStationActor::CapturePotRecord() const
{
	FCSCraftStationSaveRecord Record;
	Record.StationName = GetFName();
	auto ToRecord = [](const ACSProductPickupActor* Product)
	{
		FCSLooseProductRecord ProductRecord;
		ProductRecord.ActorClass = FSoftClassPath(Product->GetClass());
		ProductRecord.ProductRow = FCSSaveRowRef::FromHandle(Product->GetProductRow());
		ProductRecord.Transform = Product->GetActorTransform();
		ProductRecord.StorageShelfType = Product->GetStorageShelfType();
		ProductRecord.ProductState = CSSaveRecordUtils::ToRecord(
			Product->GetProductRuntimeState(), Product->GetCurrentFreshness());
		return ProductRecord;
	};
	for (const ACSProductPickupActor* Product : IngredientSlots)
	{
		if (IsValid(Product))
		{
			Record.PotProducts.Add(ToRecord(Product));
		}
	}
	for (const ACSProductPickupActor* Product : ResultSlots)
	{
		if (IsValid(Product))
		{
			Record.ResultProducts.Add(ToRecord(Product));
		}
	}
	return Record;
}

void ACSCraftStationActor::RestorePotRecord(const FCSCraftStationSaveRecord& Record)
{
	// 지켜야 하는 건 플레이어가 산 물건이지 진행 상태가 아니라서 진행은 버리고 재료만 대기 상태로 되돌린다
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BubbleTimerHandle);
	}
	for (TObjectPtr<ACSProductPickupActor>& Product : IngredientSlots)
	{
		if (IsValid(Product))
		{
			Product->Destroy();
		}
		Product = nullptr;
	}
	for (TObjectPtr<ACSProductPickupActor>& Product : ResultSlots)
	{
		if (IsValid(Product))
		{
			Product->Destroy();
		}
		Product = nullptr;
	}
	bIngredientsLocked = false;
	StirCount = 0;
	CurrentPopCount = 0;
	PopsUsed = 0;
	PopHits = 0;
	LastPoppedPeakIndex = 0;
	bFinishRetryPending = false;
	CurrentBubblePeriod = 0.0f;
	SetHeat(ECSCraftHeat::None);
	SetStep(ECSCraftStep::Idle);

	for (int32 Index = 0; Index < Record.PotProducts.Num(); ++Index)
	{
		if (Index >= IngredientSlotCount)
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] %s: 저장된 솥 재료가 슬롯보다 많아 %d개를 버립니다."),
				*GetName(), Record.PotProducts.Num() - IngredientSlotCount);
			break;
		}
		IngredientSlots[Index] = SpawnProductFromRecord(Record.PotProducts[Index], IngredientDropPoint);
	}
	for (int32 Index = 0; Index < Record.ResultProducts.Num(); ++Index)
	{
		if (Index >= ResultSlotCount)
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] %s: 저장된 결과물이 슬롯보다 많아 %d개를 버립니다."),
				*GetName(), Record.ResultProducts.Num() - ResultSlotCount);
			break;
		}
		ResultSlots[Index] = SpawnProductFromRecord(Record.ResultProducts[Index], GetResultModel(Index));
	}
	RefreshModels();
	RefreshInfoWidget();
}

ACSProductPickupActor* ACSCraftStationActor::SpawnProductFromRecord(const FCSLooseProductRecord& Record, const USceneComponent* AtModel)
{
	UWorld* World = GetWorld();
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	UClass* ProductClass = Record.ActorClass.TryLoadClass<ACSProductPickupActor>();
	const FDataTableRowHandle ProductRow = Record.ProductRow.ToHandle(IsValid(Crafting) ? Crafting->GetProductTable() : nullptr);
	if (World == nullptr || ProductClass == nullptr || ProductRow.DataTable == nullptr || ProductRow.RowName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: 저장된 솥 물건 '%s'를 되살리지 못했습니다."),
			*GetName(), *Record.ProductRow.RowName.ToString());
		return nullptr;
	}

	const FTransform SpawnTransform = IsValid(AtModel) ? LocationAndRotationOnly(AtModel) : GetActorTransform();
	ACSProductPickupActor* Product = World->SpawnActorDeferred<ACSProductPickupActor>(
		ProductClass, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Product) || !Product->InitializeProduct(ProductRow))
	{
		if (IsValid(Product))
		{
			Product->Destroy();
		}
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: 저장된 솥 물건 '%s'를 되살리지 못했습니다."),
			*GetName(), *Record.ProductRow.RowName.ToString());
		return nullptr;
	}
	Product->FinishSpawning(SpawnTransform);
	if (!IsValid(Product))
	{
		return nullptr;
	}
	Product->SetStorageShelfType(Record.StorageShelfType);
	Product->ApplyProductRuntimeState(CSSaveRecordUtils::ToRuntime(Record.ProductState, World->GetTimeSeconds()));
	StoreProductHidden(Product);
	return Product;
}

void ACSCraftStationActor::ApplyModelVisual(UStaticMeshComponent* Model, int32 ResultIndex, const ACSProductPickupActor* Product)
{
	const FTransform BaseTransform = ResultModelBaseTransforms.IsValidIndex(ResultIndex)
		? ResultModelBaseTransforms[ResultIndex]
		: Model->GetRelativeTransform();

	if (!IsValid(Product))
	{
		Model->SetStaticMesh(nullptr);
		Model->SetRelativeTransform(BaseTransform);
		Model->SetVisibility(false);
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	UStaticMesh* Mesh = Product->GetFallbackMesh();
	FTransform MeshTransform = Product->GetFallbackMeshRelativeTransform();
	if (const UCSProductVisualRegistry* Registry = ResolveVisualRegistry(Product))
	{
		if (const FCSProductVisualData* VisualData = Registry->FindVisualData(Product->GetProductId()))
		{
			if (UStaticMesh* RegistryMesh = VisualData->ProductMesh.LoadSynchronous())
			{
				Mesh = RegistryMesh;
				MeshTransform = VisualData->MeshRelativeTransform;
			}
		}
	}

	Model->SetStaticMesh(Mesh);
	Model->SetRelativeTransform(MeshTransform * BaseTransform);
	Model->SetVisibility(true);
	Model->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void ACSCraftStationActor::RefreshModels()
{
	for (int32 Index = 0; Index < ResultSlotCount; ++Index)
	{
		ApplyModelVisual(GetResultModel(Index), Index, ResultSlots[Index]);
	}
}

// =====================================================================
// E 상호작용
// =====================================================================

void ACSCraftStationActor::Interact_Implementation(AActor* Interactor)
{
	APawn* InteractingPawn = Cast<APawn>(Interactor);
	APlayerController* PlayerController = InteractingPawn ? Cast<APlayerController>(InteractingPawn->GetController()) : nullptr;
	if (!IsValid(PlayerController) || bCloseupActive)
	{
		return;
	}

	int32 SlotIndex = INDEX_NONE;
	switch (TraceAimTarget(PlayerController, SlotIndex))
	{
	case EAimTarget::SummonCircle:
		OpenSummonList(PlayerController);
		return;
	case EAimTarget::Result:
	{
		// 잠깐 쓰고 지워질 결과물이라 받게 두면 회수하기 전에 손이나 진열대로 옮겨 간다
		if (IsValid(ResultSlots[SlotIndex]) && ResultSlots[SlotIndex]->ActorHasTag(CSProductTags::TemporaryProduct))
		{
			ShowBanner(TEXT("TemporaryResultLocked"));
			return;
		}
		const FName ProductId = IsValid(ResultSlots[SlotIndex]) ? ResultSlots[SlotIndex]->GetProductId() : NAME_None;
		if (!RetrieveFromSlot(InteractingPawn, true, SlotIndex))
		{
			return;
		}
		// 대성공 2개를 E 한 번에 받는다. 두 번째는 손에 든 첫 번째와 합쳐져 포장 상자가 된다
		for (int32 Index = 0; Index < ResultSlotCount; ++Index)
		{
			if (IsValid(ResultSlots[Index]) && ResultSlots[Index]->GetProductId() == ProductId)
			{
				RetrieveFromSlot(InteractingPawn, true, Index);
			}
		}
		return;
	}
	default:
		// 다시 쏜 트레이스가 빗나가도 몸통으로 친다
		EnterCloseup(PlayerController);
		return;
	}
}

FText ACSCraftStationActor::GetInteractionText_Implementation() const
{
	if (bCloseupActive)
	{
		return FText::GetEmpty();
	}

	int32 SlotIndex = INDEX_NONE;
	switch (TraceAimTarget(GetLocalStoreController(), SlotIndex))
	{
	case EAimTarget::SummonCircle:
		return GetMessage(TEXT("SummonAimPrompt"));
	case EAimTarget::Result:
		return IsValid(ResultSlots[SlotIndex]) && ResultSlots[SlotIndex]->ActorHasTag(CSProductTags::TemporaryProduct)
			? GetMessage(TEXT("TemporaryResultLocked"))
			: GetMessage(TEXT("InteractRetrieveResult"));
	default:
		return GetMessage(TEXT("InteractCloseup"));
	}
}

bool ACSCraftStationActor::RetrieveFromSlot(APawn* InteractingPawn, bool bResultSlot, int32 SlotIndex)
{
	TObjectPtr<ACSProductPickupActor>* Slot = bResultSlot
		? (SlotIndex >= 0 && SlotIndex < ResultSlotCount ? &ResultSlots[SlotIndex] : nullptr)
		: (SlotIndex >= 0 && SlotIndex < IngredientSlotCount ? &IngredientSlots[SlotIndex] : nullptr);
	if (Slot == nullptr || !IsValid(*Slot))
	{
		return false;
	}

	ACSProductPickupActor* Product = *Slot;
	*Slot = nullptr;
	RestoreProductVisible(Product, bResultSlot ? GetResultModel(SlotIndex) : IngredientDropPoint.Get());

	// 빈손이면 손에 들고, 같은 상품 상자를 들고 있으면 상자에 담긴다
	ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(InteractingPawn);
	const bool bCollected = IsValid(Player) && Player->TryCollectProduct(Product);
	if (!bCollected)
	{
		// 못 집으면 슬롯에 도로 넣는다
		*Slot = Product;
		StoreProductHidden(Product);
	}

	RefreshModels();
	RefreshInfoWidget();
	return bCollected;
}

// =====================================================================
// 창고 재료와 마법진
// =====================================================================

FText ACSCraftStationActor::GetProductDisplayName(FName ProductId) const
{
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	const UDataTable* ProductTable = IsValid(Crafting) ? Crafting->GetProductTable() : nullptr;
	const FCSProductDataRow* ProductRow = IsValid(ProductTable)
		? ProductTable->FindRow<FCSProductDataRow>(ProductId, TEXT(""), false)
		: nullptr;
	return ProductRow != nullptr && !ProductRow->DisplayName.IsEmpty() ? ProductRow->DisplayName : FText::FromName(ProductId);
}

TSoftObjectPtr<UTexture2D> ACSCraftStationActor::FindProductIcon(FName ProductId) const
{
	// 결과물처럼 액터가 아직 없는 상품도 아이콘을 찾도록 상품 클래스 기본값의 레지스트리까지 본다
	const ACSProductPickupActor* ProductDefaults = ProductActorClass ? ProductActorClass->GetDefaultObject<ACSProductPickupActor>() : nullptr;
	const UCSProductVisualRegistry* Registry = ResolveVisualRegistry(ProductDefaults);
	const FCSProductVisualData* VisualData = Registry ? Registry->FindVisualData(ProductId) : nullptr;
	return VisualData ? VisualData->ProductIcon : TSoftObjectPtr<UTexture2D>();
}

TMap<FName, FCSCraftStorageStock> ACSCraftStationActor::CollectStorageStock() const
{
	TMap<FName, FCSCraftStorageStock> Stocks;
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Stocks;
	}

	auto AddStock = [&Stocks](FName ProductId, int32 Count)
	{
		FCSCraftStorageStock& Stock = Stocks.FindOrAdd(ProductId);
		Stock.ProductId = ProductId;
		Stock.Count += Count;
	};

	// 창고는 보관한 상자와 낱개를 전부 자기에게 붙여 둔다. 보관 배열은 밖에서 못 읽어서 부착 목록으로 센다
	for (TActorIterator<ACSFreshnessSafeZoneActor> It(World); It; ++It)
	{
		TArray<AActor*> StoredActors;
		It->GetAttachedActors(StoredActors);
		for (const AActor* StoredActor : StoredActors)
		{
			if (const ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(StoredActor))
			{
				// 직원이 옮기려고 잡아 둔 상자를 꺼내면 직원 업무가 빈 상자를 쫓는다
				if (IsValid(Crate) && !Crate->IsReservedByEmployee() && !Crate->GetProductId().IsNone())
				{
					if (const int32 Usable = CountUsableCrateUnits(Crate); Usable > 0)
					{
						AddStock(Crate->GetProductId(), Usable);
					}
				}
			}
			else if (const ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(StoredActor))
			{
				// 잠깐 쓰고 지워질 상품을 소환·자동 제조 재료로 쓰면 지울 수 없는 물건이 생긴다
				if (ACSDeliveryCrateActor::CanPackProduct(Product) && Product->IsSellable() &&
					!Product->ActorHasTag(CSProductTags::TemporaryProduct))
				{
					AddStock(Product->GetProductId(), 1);
				}
			}
		}
	}

	for (TPair<FName, FCSCraftStorageStock>& Pair : Stocks)
	{
		Pair.Value.DisplayName = GetProductDisplayName(Pair.Key);
		Pair.Value.Icon = FindProductIcon(Pair.Key);
	}
	return Stocks;
}

TArray<FCSCraftStorageStock> ACSCraftStationActor::GetStorageStockList() const
{
	TArray<FCSCraftStorageStock> List;
	CollectStorageStock().GenerateValueArray(List);
	List.Sort([](const FCSCraftStorageStock& A, const FCSCraftStorageStock& B)
	{
		return A.DisplayName.CompareTo(B.DisplayName) < 0;
	});
	return List;
}

int32 ACSCraftStationActor::CountStorageStock(FName ProductId) const
{
	const FCSCraftStorageStock* Stock = CollectStorageStock().Find(ProductId);
	return Stock ? Stock->Count : 0;
}

bool ACSCraftStationActor::TakeFromStorage(FName ProductId, int32 Count, TArray<ACSProductPickupActor*>& OutProducts)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Count <= 0)
	{
		return Count <= 0;
	}

	TArray<ACSProductPickupActor*> LooseProducts;
	TArray<ACSDeliveryCrateActor*> Crates;
	for (TActorIterator<ACSFreshnessSafeZoneActor> It(World); It; ++It)
	{
		TArray<AActor*> StoredActors;
		It->GetAttachedActors(StoredActors);
		for (AActor* StoredActor : StoredActors)
		{
			if (ACSDeliveryCrateActor* Crate = Cast<ACSDeliveryCrateActor>(StoredActor))
			{
				if (IsValid(Crate) && !Crate->IsReservedByEmployee() && Crate->GetRemainingQuantity() > 0 &&
					Crate->GetProductId() == ProductId)
				{
					Crates.Add(Crate);
				}
			}
			else if (ACSProductPickupActor* Product = Cast<ACSProductPickupActor>(StoredActor))
			{
				if (ACSDeliveryCrateActor::CanPackProduct(Product) && Product->IsSellable() && Product->GetProductId() == ProductId &&
					!Product->ActorHasTag(CSProductTags::TemporaryProduct))
				{
					LooseProducts.Add(Product);
				}
			}
		}
	}

	int32 Taken = 0;
	for (ACSProductPickupActor* Product : LooseProducts)
	{
		if (Taken >= Count)
		{
			break;
		}
		if (ACSFreshnessSafeZoneActor* Storage = Cast<ACSFreshnessSafeZoneActor>(Product->GetAttachParentActor()))
		{
			Storage->RemoveStoredLooseProduct(Product);
		}
		Product->PrepareForPickupPhysics();
		Product->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		OutProducts.Add(Product);
		++Taken;
	}

	// 적은 상자부터 비워야 창고 칸이 빨리 빈다
	Crates.Sort([](const ACSDeliveryCrateActor& A, const ACSDeliveryCrateActor& B)
	{
		return A.GetRemainingQuantity() < B.GetRemainingQuantity();
	});
	for (ACSDeliveryCrateActor* Crate : Crates)
	{
		ACSFreshnessSafeZoneActor* Storage = Cast<ACSFreshnessSafeZoneActor>(Crate->GetAttachParentActor());
		int32 Usable = CountUsableCrateUnits(Crate);
		// 판정이 어긋나도 상한 상품만 계속 돌리지 않게 한 바퀴로 막는다
		int32 RotationsLeft = Crate->GetRemainingQuantity();
		while (Taken < Count && Usable > 0 && IsValid(Crate) && Crate->GetRemainingQuantity() > 0)
		{
			ACSProductPickupActor* Product = Crate->SpawnProductActor();
			if (!IsValid(Product))
			{
				break;
			}
			// 상한 상품이 앞에 있으면 상자 맨 뒤로 돌려 넣고 뒤의 멀쩡한 상품을 꺼낸다
			if (!Product->IsSellable())
			{
				if (!Crate->IsPackingCrate() || RotationsLeft-- <= 0 || Crate->GetRemainingQuantity() < 2)
				{
					Product->Destroy();
					break;
				}
				Crate->ConsumeOne();
				if (!Crate->TryAddPackedProduct(Product, nullptr))
				{
					// 지우면 상품 하나가 그냥 사라지니 꺼낸 자리에 남긴다
					UE_LOG(LogTemp, Warning, TEXT("[제조] 상한 '%s'를 상자에 다시 넣지 못해 상자 자리에 남깁니다."), *ProductId.ToString());
					break;
				}
				continue;
			}
			--Usable;
			// 마지막 1개를 빼면 상자가 스스로 사라지므로 창고의 신선도 보호 등록을 먼저 푼다
			if (Crate->GetRemainingQuantity() == 1 && IsValid(Storage))
			{
				Storage->RemoveStoredCrate(Crate);
			}
			Crate->ConsumeOne();
			OutProducts.Add(Product);
			++Taken;
		}
	}
	return Taken >= Count;
}

bool ACSCraftStationActor::IsSummonCircleOccupied() const
{
	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(SummonArea))
	{
		return false;
	}

	const FTransform AreaTransform = SummonArea->GetComponentTransform();
	const FVector Extent = SummonArea->GetUnscaledBoxExtent();
	const FBox LocalArea(-Extent, Extent);
	// 물리로 떨어진 메시는 액터 루트에서 떨어져 움직여서 액터 위치 대신 메시 범위 중심을 본다
	auto IsOnCircle = [&](const AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsHidden() || Actor->GetAttachParentActor() != nullptr)
		{
			return false;
		}
		const FBox Bounds = Actor->GetComponentsBoundingBox(true);
		return Bounds.IsValid && LocalArea.IsInsideOrOn(AreaTransform.InverseTransformPosition(Bounds.GetCenter()));
	};

	for (TActorIterator<ACSProductPickupActor> It(World); It; ++It)
	{
		if (!It->IsA<ACSProductPlacementPreviewActor>() && IsOnCircle(*It))
		{
			return true;
		}
	}
	for (TActorIterator<ACSDeliveryCrateActor> It(World); It; ++It)
	{
		if (IsOnCircle(*It))
		{
			return true;
		}
	}
	return false;
}

FVector ACSCraftStationActor::GetCircleDropLocation(int32 Index) const
{
	if (!IsValid(SummonArea))
	{
		return GetActorLocation();
	}
	// 4열 격자. 자동 제조 실패 때 재료 여러 개를 흩어 놓아도 서로 겹치지 않게 한다
	constexpr int32 Columns = 4;
	constexpr float Spacing = 22.0f;
	const float X = (Index % Columns - (Columns - 1) * 0.5f) * Spacing;
	const float Y = (Index / Columns) * Spacing;
	return SummonArea->GetComponentTransform().TransformPositionNoScale(FVector(X, Y, 0.0f));
}

FVector ACSCraftStationActor::GetCircleSlotLocation(int32 SlotIndex) const
{
	if (!IsValid(SummonArea))
	{
		return GetActorLocation();
	}
	// 3열 2행. 종류마다 상자 하나가 이 자리에 떨어진다
	constexpr int32 Columns = 3;
	constexpr float Spacing = 60.0f;
	const float X = (SlotIndex % Columns - 1) * Spacing;
	const float Y = (SlotIndex / Columns - 0.5f) * Spacing;
	return SummonArea->GetComponentTransform().TransformPositionNoScale(FVector(X, Y, 0.0f));
}

void ACSCraftStationActor::ScatterProductsOnCircle(const TArray<ACSProductPickupActor*>& Products)
{
	const FRotator Rotation(0.0f, GetActorRotation().Yaw, 0.0f);
	for (int32 Index = 0; Index < Products.Num(); ++Index)
	{
		ACSProductPickupActor* Product = Products[Index];
		if (!IsValid(Product))
		{
			continue;
		}
		Product->SetActorLocationAndRotation(GetCircleDropLocation(Index), Rotation);
		Product->SetActorHiddenInGame(false);
		Product->SetActorEnableCollision(true);
		Product->EnableWorldDropPhysics();
	}
}

bool ACSCraftStationActor::PlaceProductsOnCircle(const TArray<ACSProductPickupActor*>& Products, const FVector& Location)
{
	if (Products.IsEmpty() || !IsValid(Products[0]))
	{
		return false;
	}
	const FRotator Rotation(0.0f, GetActorRotation().Yaw, 0.0f);
	if (Products.Num() == 1)
	{
		ACSProductPickupActor* Product = Products[0];
		Product->SetActorLocationAndRotation(Location, Rotation);
		Product->SetActorHiddenInGame(false);
		Product->SetActorEnableCollision(true);
		Product->EnableWorldDropPhysics();
		return true;
	}
	if (!PackingCrateClass || Products.Num() > ACSDeliveryCrateActor::PackingCapacity)
	{
		return false;
	}

	const FTransform SpawnTransform(Rotation, Location);
	ACSDeliveryCrateActor* Crate = GetWorld()->SpawnActorDeferred<ACSDeliveryCrateActor>(
		PackingCrateClass, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Crate))
	{
		return false;
	}
	if (!Crate->InitializePackedProducts(Products))
	{
		Crate->Destroy();
		return false;
	}
	Crate->FinishSpawning(SpawnTransform);
	if (!IsValid(Crate))
	{
		return false;
	}
	Crate->EnableWorldDropPhysics();

	// 상자가 생긴 뒤에만 원본을 지운다. 상자가 상품마다 신선도를 따로 들고 있다
	for (ACSProductPickupActor* Product : Products)
	{
		ACSDeliveryCrateActor::RemovePackedSource(Product);
	}
	return true;
}

bool ACSCraftStationActor::SpawnFreshProducts(
	FName ProductId,
	const TArray<FTransform>& SpawnTransforms,
	TArray<ACSProductPickupActor*>& OutProducts)
{
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	UDataTable* ProductTable = IsValid(Crafting) ? Crafting->GetProductTable() : nullptr;
	if (!ProductActorClass || !IsValid(ProductTable) ||
		ProductTable->FindRow<FCSProductDataRow>(ProductId, TEXT(""), false) == nullptr)
	{
		return false;
	}

	FDataTableRowHandle ProductRow;
	ProductRow.DataTable = ProductTable;
	ProductRow.RowName = ProductId;

	auto DestroySpawned = [&OutProducts]()
	{
		for (ACSProductPickupActor* Spawned : OutProducts)
		{
			Spawned->Destroy();
		}
		OutProducts.Reset();
	};

	for (const FTransform& SpawnTransform : SpawnTransforms)
	{
		ACSProductPickupActor* Product = GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
			ProductActorClass, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		// ApplyProductRuntimeState를 안 불러야 신선도와 유통기한이 만든 시점부터 시작한다
		if (!IsValid(Product) || !Product->InitializeProduct(ProductRow))
		{
			if (IsValid(Product))
			{
				Product->Destroy();
			}
			DestroySpawned();
			return false;
		}
		Product->FinishSpawning(SpawnTransform);
		if (!IsValid(Product))
		{
			DestroySpawned();
			return false;
		}
		OutProducts.Add(Product);
	}
	return true;
}

bool ACSCraftStationActor::SpawnFreshResultsOnCircle(FName ProductId, int32 Count)
{
	TArray<FTransform> SpawnTransforms;
	for (int32 Index = 0; Index < FMath::Clamp(Count, 1, MaxCircleQuantity); ++Index)
	{
		SpawnTransforms.Add(FTransform(FRotator(0.0f, GetActorRotation().Yaw, 0.0f), GetCircleDropLocation(Index)));
	}

	TArray<ACSProductPickupActor*> Products;
	if (!SpawnFreshProducts(ProductId, SpawnTransforms, Products))
	{
		return false;
	}
	if (!IsValid(SummonArea) || !PlaceProductsOnCircle(Products, SummonArea->GetComponentLocation()))
	{
		for (ACSProductPickupActor* Product : Products)
		{
			Product->Destroy();
		}
		return false;
	}
	return true;
}

TArray<ACSProductPickupActor*> ACSCraftStationActor::PlaceFreshProductsOnCircle(const TArray<FName>& ProductIds)
{
	TArray<ACSProductPickupActor*> Placed;
	const FRotator Rotation(0.0f, GetActorRotation().Yaw, 0.0f);
	for (int32 Index = 0; Index < ProductIds.Num(); ++Index)
	{
		const FVector Location = GetCircleSlotLocation(Index);
		TArray<ACSProductPickupActor*> Spawned;
		if (!SpawnFreshProducts(ProductIds[Index], { FTransform(Rotation, Location) }, Spawned) ||
			!PlaceProductsOnCircle(Spawned, Location))
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] %s를 마법진에 놓지 못했습니다."), *ProductIds[Index].ToString());
			for (ACSProductPickupActor* Product : Spawned)
			{
				Product->Destroy();
			}
			continue;
		}
		Placed.Append(Spawned);
	}
	return Placed;
}

// =====================================================================
// 소환 목록
// =====================================================================

void ACSCraftStationActor::OpenSummonList(APlayerController* PlayerController)
{
	// 클로즈업 중에는 폰이 숨어 있어 마법진을 조준할 수 없지만, 들어오면 두 입력 방식이 겹친다
	ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(PlayerController);
	if (bCloseupActive || IsValid(ActiveSummonListWidget) || !IsValid(StoreController))
	{
		return;
	}
	// 비어 있으면 소환 버튼을 누르는 순간 상자를 못 만든다
	if (!SummonListWidgetClass || !PackingCrateClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: SummonListWidgetClass / PackingCrateClass를 지정해 주세요. 소환 목록을 열지 않습니다."), *GetName());
		return;
	}

	ActiveSummonListWidget = CreateWidget<UCSCraftSummonListWidget>(StoreController, SummonListWidgetClass);
	if (!IsValid(ActiveSummonListWidget))
	{
		return;
	}
	ActiveSummonListWidget->AddToViewport(20);
	StoreController->SetGameplayUIInputLock(ActiveSummonListWidget, true);
	ActiveSummonListWidget->SetStation(this);
}

void ACSCraftStationActor::CloseSummonList()
{
	UCSCraftSummonListWidget* Widget = ActiveSummonListWidget;
	ActiveSummonListWidget = nullptr;
	if (!IsValid(Widget))
	{
		return;
	}
	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Widget->GetOwningPlayer()))
	{
		StoreController->SetGameplayUIInputLock(Widget, false);
	}
	Widget->RemoveFromParent();
}

FText ACSCraftStationActor::RequestSummon(const TMap<FName, int32>& Selections, bool& bOutSummoned)
{
	bOutSummoned = false;
	if (Selections.IsEmpty())
	{
		return FText::GetEmpty();
	}
	if (Selections.Num() > MaxSummonKinds)
	{
		return FText::Format(GetMessage(TEXT("SummonTooManyKinds")), FText::AsNumber(MaxSummonKinds));
	}
	if (IsSummonCircleOccupied() || !IsValid(SummonArea))
	{
		return GetMessage(TEXT("SummonCircleOccupied"));
	}
	// 창을 연 뒤 직원이 창고에서 가져갔을 수 있다. 일부만 소환되면 무엇이 빠졌는지 헷갈려서 먼저 전부 확인한다
	const TMap<FName, FCSCraftStorageStock> Stocks = CollectStorageStock();
	for (const TPair<FName, int32>& Selection : Selections)
	{
		const FCSCraftStorageStock* Stock = Stocks.Find(Selection.Key);
		if (Stock == nullptr || Stock->Count < FMath::Clamp(Selection.Value, 1, MaxCircleQuantity))
		{
			return GetMessage(TEXT("SummonNotEnough"));
		}
	}

	// 한 종류면 가운데, 여러 종류면 종류마다 자리를 나눠 놓는다
	const bool bSingleKind = Selections.Num() == 1;
	int32 SlotIndex = 0;
	int32 TotalCount = 0;
	FName LastProductId;
	for (const TPair<FName, int32>& Selection : Selections)
	{
		const int32 Count = FMath::Clamp(Selection.Value, 1, MaxCircleQuantity);
		TArray<ACSProductPickupActor*> Products;
		if (!TakeFromStorage(Selection.Key, Count, Products))
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] '%s' %d개 중 %d개만 창고에서 꺼냈습니다."), *Selection.Key.ToString(), Count, Products.Num());
		}
		if (Products.IsEmpty())
		{
			continue;
		}
		const FVector Location = bSingleKind ? SummonArea->GetComponentLocation() : GetCircleSlotLocation(SlotIndex);
		if (!PlaceProductsOnCircle(Products, Location))
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] 포장 상자를 만들지 못해 '%s'를 낱개로 놓습니다."), *Selection.Key.ToString());
			ScatterProductsOnCircle(Products);
		}
		++SlotIndex;
		TotalCount += Products.Num();
		LastProductId = Selection.Key;
	}

	if (TotalCount == 0)
	{
		return GetMessage(TEXT("SummonNotEnough"));
	}
	bOutSummoned = true;
	if (SlotIndex == 1)
	{
		return FText::Format(GetMessage(TEXT("SummonDone")), GetProductDisplayName(LastProductId), FText::AsNumber(TotalCount));
	}
	return FText::Format(GetMessage(TEXT("SummonDoneMany")), FText::AsNumber(SlotIndex), FText::AsNumber(TotalCount));
}

// =====================================================================
// 자동 제조
// =====================================================================

TArray<FCSCraftAutoOption> ACSCraftStationActor::GetAutoCraftOptions() const
{
	TArray<FCSCraftAutoOption> Options;
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	if (!IsValid(Crafting) || !Crafting->IsCraftingAvailable())
	{
		return Options;
	}

	const TMap<FName, FCSCraftStorageStock> Stocks = CollectStorageStock();
	auto Owned = [&Stocks](FName ProductId)
	{
		const FCSCraftStorageStock* Stock = Stocks.Find(ProductId);
		return Stock ? Stock->Count : 0;
	};

	for (const FName RecipeRowName : Crafting->GetDiscoveredRecipeRows())
	{
		const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(RecipeRowName);
		if (Recipe == nullptr || Recipe->RecipeKind == ECSCraftRecipeKind::Junk)
		{
			continue;
		}
		FCSCraftAutoOption& Option = Options.AddDefaulted_GetRef();
		Option.RecipeRowName = RecipeRowName;
		Option.ResultName = GetProductDisplayName(Recipe->ResultProduct);
		Option.ResultIcon = FindProductIcon(Recipe->ResultProduct);
		Option.IngredientAName = GetProductDisplayName(Recipe->IngredientA);
		Option.IngredientANeed = FMath::Max(1, Recipe->IngredientACount);
		Option.IngredientAOwned = Owned(Recipe->IngredientA);
		Option.IngredientBName = GetProductDisplayName(Recipe->IngredientB);
		Option.IngredientBNeed = FMath::Max(1, Recipe->IngredientBCount);
		Option.IngredientBOwned = Owned(Recipe->IngredientB);
		Option.MaxSets = FMath::Min3(
			Option.IngredientAOwned / Option.IngredientANeed,
			Option.IngredientBOwned / Option.IngredientBNeed,
			MaxCircleQuantity);
	}
	Options.Sort([](const FCSCraftAutoOption& A, const FCSCraftAutoOption& B)
	{
		return A.ResultName.CompareTo(B.ResultName) < 0;
	});
	return Options;
}

void ACSCraftStationActor::HandleAutoCraftOpen()
{
	if (!bCloseupActive || IsAnyCloseupPanelOpen())
	{
		return;
	}
	if (!IsBeforeCraftStart() || !CountIngredients().IsEmpty())
	{
		ShowBanner(TEXT("AutoCraftNeedEmptyPot"));
		return;
	}
	if (GetAutoCraftOptions().IsEmpty())
	{
		ShowBanner(TEXT("AutoCraftNoRecipe"));
		return;
	}

	ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(CloseupController);
	if (!IsValid(StoreController))
	{
		return;
	}
	if (!AutoPanelWidgetClass || !PackingCrateClass || !ProductActorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: AutoPanelWidgetClass / PackingCrateClass / ProductActorClass를 지정해 주세요. 자동 제조 창을 열지 않습니다."), *GetName());
		return;
	}

	ActiveAutoPanelWidget = CreateWidget<UCSCraftAutoPanelWidget>(StoreController, AutoPanelWidgetClass);
	if (!IsValid(ActiveAutoPanelWidget))
	{
		return;
	}
	ActiveAutoPanelWidget->AddToViewport(20);
	StoreController->SetGameplayUIInputLock(ActiveAutoPanelWidget, true);
	ActiveAutoPanelWidget->SetStation(this);
}

void ACSCraftStationActor::CloseAutoPanel()
{
	UCSCraftAutoPanelWidget* Widget = ActiveAutoPanelWidget;
	ActiveAutoPanelWidget = nullptr;
	if (!IsValid(Widget))
	{
		return;
	}
	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Widget->GetOwningPlayer()))
	{
		StoreController->SetGameplayUIInputLock(Widget, false);
	}
	Widget->RemoveFromParent();
}

void ACSCraftStationActor::HandleRecipeBookOpen()
{
	if (!bCloseupActive || IsAnyCloseupPanelOpen())
	{
		return;
	}
	// 도감은 재료를 쓰지 않으니 솥에 재료가 있어도 연다
	if (!IsBeforeCraftStart())
	{
		ShowBanner(TEXT("RecipeBookLocked"));
		return;
	}

	ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(CloseupController);
	if (!IsValid(StoreController))
	{
		return;
	}
	if (!RecipeBookWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: RecipeBookWidgetClass를 지정해 주세요. 도감을 열지 않습니다."), *GetName());
		return;
	}

	ActiveRecipeBookWidget = CreateWidget<UCSCraftRecipeBookWidget>(StoreController, RecipeBookWidgetClass);
	if (!IsValid(ActiveRecipeBookWidget))
	{
		return;
	}
	ActiveRecipeBookWidget->AddToViewport(20);
	StoreController->SetGameplayUIInputLock(ActiveRecipeBookWidget, true);
	ActiveRecipeBookWidget->SetStation(this);
}

void ACSCraftStationActor::CloseRecipeBook()
{
	UCSCraftRecipeBookWidget* Widget = ActiveRecipeBookWidget;
	ActiveRecipeBookWidget = nullptr;
	if (!IsValid(Widget))
	{
		return;
	}
	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Widget->GetOwningPlayer()))
	{
		StoreController->SetGameplayUIInputLock(Widget, false);
	}
	Widget->RemoveFromParent();
}

void ACSCraftStationActor::HandleIngredientPanelOpen()
{
	if (!bCloseupActive || IsAnyCloseupPanelOpen())
	{
		return;
	}
	if (!IsBeforeCraftStart())
	{
		ShowBanner(MsgIngredientLocked);
		return;
	}

	ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(CloseupController);
	if (!IsValid(StoreController))
	{
		return;
	}
	if (!IngredientPanelWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: IngredientPanelWidgetClass를 지정해 주세요. 재료 창을 열지 않습니다."), *GetName());
		return;
	}

	ActiveIngredientPanelWidget = CreateWidget<UCSCraftIngredientPanelWidget>(StoreController, IngredientPanelWidgetClass);
	if (!IsValid(ActiveIngredientPanelWidget))
	{
		return;
	}
	ActiveIngredientPanelWidget->AddToViewport(20);
	StoreController->SetGameplayUIInputLock(ActiveIngredientPanelWidget, true);
	ActiveIngredientPanelWidget->SetStation(this);
}

void ACSCraftStationActor::CloseIngredientPanel()
{
	UCSCraftIngredientPanelWidget* Widget = ActiveIngredientPanelWidget;
	ActiveIngredientPanelWidget = nullptr;
	if (!IsValid(Widget))
	{
		return;
	}
	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Widget->GetOwningPlayer()))
	{
		StoreController->SetGameplayUIInputLock(Widget, false);
	}
	Widget->RemoveFromParent();
}

TArray<FCSCraftIngredientEntry> ACSCraftStationActor::GetIngredientEntries() const
{
	TArray<FCSCraftIngredientEntry> Entries;
	for (const ACSProductPickupActor* Product : IngredientSlots)
	{
		if (!IsValid(Product))
		{
			continue;
		}
		const FName ProductId = Product->GetProductId();
		FCSCraftIngredientEntry* Entry = Entries.FindByPredicate(
			[ProductId](const FCSCraftIngredientEntry& Existing) { return Existing.ProductId == ProductId; });
		if (Entry == nullptr)
		{
			Entry = &Entries.AddDefaulted_GetRef();
			Entry->ProductId = ProductId;
			Entry->DisplayName = Product->GetProductDisplayName();
			Entry->Icon = FindProductIcon(ProductId);
		}
		++Entry->Count;
	}
	return Entries;
}

FText ACSCraftStationActor::RequestRemoveIngredient(FName ProductId)
{
	// 창이 열린 뒤 상태가 바뀌었을 수 있어서 다시 확인한다
	if (!IsBeforeCraftStart())
	{
		return GetMessage(MsgIngredientLocked);
	}
	// 같은 종류 중 가장 뒤 슬롯부터 꺼내야 앞쪽 슬롯 순서가 유지된다
	for (int32 Index = IngredientSlotCount - 1; Index >= 0; --Index)
	{
		if (IsValid(IngredientSlots[Index]) && IngredientSlots[Index]->GetProductId() == ProductId)
		{
			return RetrieveFromSlot(CloseupPawn, false, Index)
				? FText::GetEmpty()
				: GetMessage(TEXT("IngredientRemoveHandFull"));
		}
	}
	return FText::GetEmpty();
}

FText ACSCraftStationActor::RequestAutoCraft(FName RecipeRowName, int32 Sets)
{
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	if (!IsValid(Crafting) || !Crafting->IsCraftingAvailable())
	{
		return GetMessage(TEXT("AutoCraftFailed"));
	}
	const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(RecipeRowName);
	if (Recipe == nullptr || Recipe->RecipeKind == ECSCraftRecipeKind::Junk ||
		!Crafting->GetDiscoveredRecipeRows().Contains(RecipeRowName))
	{
		return GetMessage(TEXT("AutoCraftNoRecipe"));
	}

	Sets = FMath::Clamp(Sets, 1, MaxCircleQuantity);
	const int32 NeedA = FMath::Max(1, Recipe->IngredientACount) * Sets;
	const int32 NeedB = FMath::Max(1, Recipe->IngredientBCount) * Sets;
	if (CountStorageStock(Recipe->IngredientA) < NeedA || CountStorageStock(Recipe->IngredientB) < NeedB)
	{
		return GetMessage(TEXT("AutoCraftNotEnough"));
	}
	if (IsSummonCircleOccupied())
	{
		return GetMessage(TEXT("SummonCircleOccupied"));
	}

	const TMap<FName, int32> OneSet = {
		{ Recipe->IngredientA, Recipe->IngredientACount },
		{ Recipe->IngredientB, Recipe->IngredientBCount } };
	const FCSCraftEvaluation Evaluation = Crafting->EvaluateCraft(OneSet, Recipe->Heat, Recipe->StirCount, Recipe->PopSuccessHits);
	if (Evaluation.Outcome != ECSCraftOutcome::Success && Evaluation.Outcome != ECSCraftOutcome::GreatSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] 자동 제조 '%s'가 레시피 그대로 판정해도 성공이 아닙니다. DT_CraftRecipe 값을 확인해 주세요."),
			*RecipeRowName.ToString());
		return GetMessage(TEXT("AutoCraftFailed"));
	}

	// 재료는 결과가 놓인 뒤에만 지운다. 중간에 실패하면 꺼낸 재료를 마법진에 돌려 놓는다
	TArray<ACSProductPickupActor*> Ingredients;
	const bool bTaken = TakeFromStorage(Recipe->IngredientA, NeedA, Ingredients) &&
		TakeFromStorage(Recipe->IngredientB, NeedB, Ingredients);
	// 대성공 2개는 직접 만들 때의 보상이라 판정 수량과 상관없이 세트당 1개다
	if (!bTaken || !SpawnFreshResultsOnCircle(Evaluation.ResultProduct, Sets))
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] 자동 제조 '%s'에 실패해서 꺼낸 재료 %d개를 마법진에 놓습니다."),
			*RecipeRowName.ToString(), Ingredients.Num());
		ScatterProductsOnCircle(Ingredients);
		return GetMessage(TEXT("AutoCraftFailed"));
	}

	// 쓰레기통을 거치지 않아서 폐기 손실에 잡히지 않는다
	for (ACSProductPickupActor* Ingredient : Ingredients)
	{
		Ingredient->Destroy();
	}

	CloseAutoPanel();
	ShowMessage(FText::Format(
		GetMessage(TEXT("AutoCraftDone")), GetProductDisplayName(Evaluation.ResultProduct), FText::AsNumber(Sets)), false);
	return FText::GetEmpty();
}

// =====================================================================
// 클로즈업
// =====================================================================

void ACSCraftStationActor::EnterCloseup(APlayerController* PlayerController)
{
	if (bCloseupActive || !IsValid(PlayerController) || !IsValid(CloseupCamera))
	{
		return;
	}
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer());
	// IMC가 없으면 나갈 키도 없다
	if (!IsValid(CloseupMappingContext) || InputSubsystem == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: CloseupMappingContext를 지정해 주세요. 클로즈업에 들어가지 않습니다."), *GetName());
		return;
	}

	CloseupController = PlayerController;
	PreviousViewTarget = PlayerController->GetViewTarget();

	// 이전에 나갈 때 걸어 둔 복원 타이머가 남아 있을 수 있어서 먼저 되돌린다
	RestorePawnVisibility();
	if (APawn* PlayerPawn = PlayerController->GetPawn(); IsValid(PlayerPawn))
	{
		CloseupPawn = PlayerPawn;
		bPawnWasHidden = PlayerPawn->IsHidden();
		bPawnVisibilityOverridden = true;
		PlayerPawn->SetActorHiddenInGame(true);
		// 폰 입력을 끄면 이동, 시점, Q, 퀵바, E가 전부 막힌다
		PlayerPawn->DisableInput(PlayerController);
	}

	CloseupCamera->SetActive(true);
	PlayerController->SetViewTargetWithBlend(this, CameraBlendTime, EViewTargetBlendFunction::VTBlend_Cubic, 0.0f, true);

	InputSubsystem->AddMappingContext(CloseupMappingContext, CloseupMappingPriority);
	EnableInput(PlayerController);
	BindCloseupInput();

	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(PlayerController))
	{
		StoreController->SetCraftCloseupActive(true);
		StoreController->SetStoreHUDHidden(this, true);
		// 상단 바가 클로즈업 화면 위쪽 정보 창을 가린다
		StoreController->SetStoreTopBarHidden(this, true);
	}

	if (CloseupWidgetClass)
	{
		ActiveCloseupWidget = CreateWidget<UCSCraftCloseupWidget>(PlayerController, CloseupWidgetClass);
		if (IsValid(ActiveCloseupWidget))
		{
			ActiveCloseupWidget->AddToViewport(10);
			ActiveCloseupWidget->SetStep(CraftStep);
			ActiveCloseupWidget->SetGuidesHidden(bCloseupGuidesHidden);
		}
	}

	bCloseupActive = true;
	RefreshInfoWidget();
}

void ACSCraftStationActor::ExitCloseup(bool bImmediate)
{
	if (!bCloseupActive)
	{
		return;
	}
	bCloseupActive = false;
	CloseAutoPanel();
	CloseRecipeBook();
	CloseIngredientPanel();

	// 제조 상태와 거품 타이머는 그대로 둔다. 다시 들어오면 이어서 한다
	APlayerController* PlayerController = CloseupController;
	bool bStartedReturnBlend = false;
	if (IsValid(PlayerController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			InputSubsystem->RemoveMappingContext(CloseupMappingContext);
		}
		DisableInput(PlayerController);
		if (IsValid(CloseupPawn))
		{
			CloseupPawn->EnableInput(PlayerController);
		}

		AActor* RestoreTarget = IsValid(PreviousViewTarget) ? PreviousViewTarget.Get() : PlayerController->GetPawn();
		if (IsValid(RestoreTarget))
		{
			const float BlendTime = bImmediate ? 0.0f : CameraBlendTime;
			PlayerController->SetViewTargetWithBlend(RestoreTarget, BlendTime, EViewTargetBlendFunction::VTBlend_Cubic, 0.0f, true);
			bStartedReturnBlend = BlendTime > 0.0f;
		}

		if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(PlayerController))
		{
			StoreController->SetCraftCloseupActive(false);
			StoreController->SetStoreHUDHidden(this, false);
			StoreController->SetStoreTopBarHidden(this, false);
		}
	}

	if (IsValid(CloseupCamera))
	{
		CloseupCamera->SetActive(false);
	}
	if (IsValid(ActiveCloseupWidget))
	{
		ActiveCloseupWidget->RemoveFromParent();
	}
	ActiveCloseupWidget = nullptr;
	CloseupController = nullptr;
	PreviousViewTarget = nullptr;

	// 시점이 돌아오기 전에 폰이 보이면 카메라 앞에 몸이 겹친다
	UWorld* World = GetWorld();
	if (bStartedReturnBlend && World != nullptr)
	{
		World->GetTimerManager().SetTimer(
			PawnVisibilityRestoreTimerHandle, this, &ACSCraftStationActor::RestorePawnVisibility, CameraBlendTime, false);
	}
	else
	{
		RestorePawnVisibility();
	}
}

void ACSCraftStationActor::RestorePawnVisibility()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PawnVisibilityRestoreTimerHandle);
	}
	if (bPawnVisibilityOverridden && IsValid(CloseupPawn))
	{
		CloseupPawn->SetActorHiddenInGame(bPawnWasHidden);
	}
	bPawnVisibilityOverridden = false;
	bPawnWasHidden = false;
	CloseupPawn = nullptr;
}

void ACSCraftStationActor::BindCloseupInput()
{
	if (bCloseupInputBound)
	{
		return;
	}
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!IsValid(EnhancedInput))
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: 입력 컴포넌트가 EnhancedInputComponent가 아닙니다."), *GetName());
		return;
	}

	auto Bind = [this, EnhancedInput](UInputAction* Action, void (ACSCraftStationActor::*Handler)(), const TCHAR* Label)
	{
		if (IsValid(Action))
		{
			EnhancedInput->BindAction(Action, ETriggerEvent::Started, this, Handler);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[제조] %s: %s를 지정해 주세요."), *GetName(), Label);
		}
	};
	Bind(HeatDownAction, &ACSCraftStationActor::HandleHeatDown, TEXT("HeatDownAction"));
	Bind(HeatUpAction, &ACSCraftStationActor::HandleHeatUp, TEXT("HeatUpAction"));
	Bind(StirAction, &ACSCraftStationActor::HandleStir, TEXT("StirAction"));
	Bind(PopAction, &ACSCraftStationActor::HandleBubblePop, TEXT("PopAction"));
	Bind(FinishAction, &ACSCraftStationActor::HandleFinish, TEXT("FinishAction"));
	Bind(ExitAction, &ACSCraftStationActor::HandleExit, TEXT("ExitAction"));
	Bind(AutoCraftAction, &ACSCraftStationActor::HandleAutoCraftOpen, TEXT("AutoCraftAction"));
	Bind(RecipeBookAction, &ACSCraftStationActor::HandleRecipeBookOpen, TEXT("RecipeBookAction"));
	bCloseupInputBound = true;
}

// =====================================================================
// 제조 흐름
// =====================================================================

void ACSCraftStationActor::HandleHeatUp()
{
	if (CraftStep == ECSCraftStep::Idle)
	{
		SetHeat(ECSCraftHeat::Low);
		SetStep(ECSCraftStep::Heat);
	}
	else if (CraftStep == ECSCraftStep::Heat)
	{
		SetHeat(CurrentHeat == ECSCraftHeat::Low ? ECSCraftHeat::Medium : ECSCraftHeat::High);
	}
}

void ACSCraftStationActor::HandleHeatDown()
{
	if (CraftStep == ECSCraftStep::Heat)
	{
		SetHeat(CurrentHeat == ECSCraftHeat::High ? ECSCraftHeat::Medium : ECSCraftHeat::Low);
	}
}

void ACSCraftStationActor::HandleStir()
{
	if (CraftStep == ECSCraftStep::Heat)
	{
		TryStartCraft();
	}
	else if (CraftStep == ECSCraftStep::Stir)
	{
		++StirCount;
		OnStirred(StirCount);
		RefreshInfoWidget();
	}
}

void ACSCraftStationActor::TryStartCraft()
{
	const TMap<FName, int32> Counts = CountIngredients();
	if (Counts.Num() != 2)
	{
		ShowBanner(TEXT("NeedTwoKinds"));
		return;
	}
	// 결과 슬롯이 2칸이라 남은 상품이 있으면 대성공 2개를 둘 자리가 없다
	if (HasResultProduct())
	{
		ShowBanner(TEXT("ResultSlotOccupied"));
		return;
	}
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	if (!IsValid(Crafting) || !Crafting->IsCraftingAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] 제조 서브시스템이 준비되지 않아 시작하지 않습니다. GameMode의 제조 DT를 확인해 주세요."));
		return;
	}

	TArray<FName> Names;
	Counts.GetKeys(Names);
	const float RecipePeriod = Crafting->GetRecipePeriod(Names[0], Names[1]);
	CurrentBubblePeriod = RecipePeriod > 0.0f ? RecipePeriod : DefaultBubblePeriod;
	if (PopToleranceSeconds >= CurrentBubblePeriod * 0.5f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] 거품 허용폭 %.2f초가 거품 주기 %.2f초의 절반 이상이라 언제 터트려도 맞습니다."),
			PopToleranceSeconds, CurrentBubblePeriod);
	}
	const int32 RecipePopCount = Crafting->GetRecipePopCount(Names[0], Names[1]);
	CurrentPopCount = RecipePopCount > 0 ? RecipePopCount : FMath::Max(1, DefaultPopCount);
	PopsUsed = 0;
	PopHits = 0;
	LastPoppedPeakIndex = 0;
	bFinishRetryPending = false;

	bIngredientsLocked = true;
	StirCount = 1;
	CraftStartTimeSeconds = GetWorld()->GetTimeSeconds();
	SetStep(ECSCraftStep::Stir);

	OnBubbleCycle(CurrentBubblePeriod);
	GetWorld()->GetTimerManager().SetTimer(
		BubbleTimerHandle, this, &ACSCraftStationActor::HandleBubbleCycle, CurrentBubblePeriod, true);
	OnStirred(StirCount);
}

void ACSCraftStationActor::HandleBubbleCycle()
{
	OnBubbleCycle(CurrentBubblePeriod);
}

void ACSCraftStationActor::HandleBubblePop()
{
	// 재시도 대기 중에는 거품을 다 쓴 상태라 더 터트리면 횟수가 PopCount를 넘는다
	if ((CraftStep != ECSCraftStep::Stir && CraftStep != ECSCraftStep::Pop) || bFinishRetryPending)
	{
		return;
	}

	// 타이머는 프레임 단위로 늦게 불려서 판정은 경과 시간으로 한다
	// 거품 최고점은 시작 후 Period의 정수배(1배 이상)
	const float Elapsed = GetWorld()->GetTimeSeconds() - CraftStartTimeSeconds;
	const int32 PeakIndex = FMath::Max(1, FMath::RoundToInt(Elapsed / CurrentBubblePeriod));
	// 이미 터진 거품이다. 연타가 거품 수를 깎지 않게 한다
	if (PeakIndex <= LastPoppedPeakIndex)
	{
		return;
	}

	const float PeakTime = PeakIndex * CurrentBubblePeriod;
	const bool bHit = FMath::Abs(Elapsed - PeakTime) <= PopToleranceSeconds;
	++PopsUsed;
	LastPoppedPeakIndex = PeakIndex;
	if (bHit)
	{
		++PopHits;
	}

	// 허용 구간이 끝날 때까지 숨겨야 터트린 거품이 같은 최고점에서 다시 보이지 않는다
	const float HiddenSeconds = FMath::Max(0.0f, PeakTime + PopToleranceSeconds - Elapsed);
	OnBubblePopResult(bHit, GetMessage(bHit ? TEXT("PopPerfect") : TEXT("PopMiss")), HiddenSeconds);

	if (PopsUsed >= CurrentPopCount)
	{
		FinishCraft();
	}
	else if (CraftStep != ECSCraftStep::Pop)
	{
		SetStep(ECSCraftStep::Pop);
	}
	else
	{
		RefreshInfoWidget();
	}
}

void ACSCraftStationActor::HandleFinish()
{
	if (bFinishRetryPending)
	{
		FinishCraft();
		return;
	}
	HandleIngredientPanelOpen();
}

void ACSCraftStationActor::HandleExit()
{
	ExitCloseup(false);
}

void ACSCraftStationActor::FinishCraft()
{
	UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	if (!IsValid(Crafting) || !Crafting->IsCraftingAvailable())
	{
		UE_LOG(LogTemp, Error, TEXT("[제조] 제조 서브시스템이 없어 완성할 수 없습니다."));
		return;
	}

	const TMap<FName, int32> IngredientCounts = CountIngredients();
	const FCSCraftEvaluation Evaluation = Crafting->EvaluateCraft(IngredientCounts, CurrentHeat, StirCount, PopHits);
	const bool bMadeProduct = Evaluation.Outcome == ECSCraftOutcome::GreatSuccess ||
		Evaluation.Outcome == ECSCraftOutcome::Success ||
		Evaluation.Outcome == ECSCraftOutcome::Junk;

	// 재료를 지우기 전이라 거품 단계에 머물고 E로 다시 시도한다
	auto WaitForRetry = [this]()
	{
		bFinishRetryPending = true;
		SetStep(ECSCraftStep::Pop);
		ShowBanner(TEXT("FinishFailed"));
	};

	FText ResultName;
	if (bMadeProduct)
	{
		TArray<ACSProductPickupActor*> Products;
		if (!SpawnResultProducts(Evaluation, Products))
		{
			UE_LOG(LogTemp, Error, TEXT("[제조] 결과물 '%s' 스폰에 실패해서 재료를 그대로 둡니다."), *Evaluation.ResultProduct.ToString());
			WaitForRetry();
			return;
		}
		ResultName = Products[0]->GetProductDisplayName();

		// 잠깐 쓰고 지워질 재료로 만든 결과물도 같이 지워져야 공짜 상품이 남지 않는다
		for (const TObjectPtr<ACSProductPickupActor>& Ingredient : IngredientSlots)
		{
			if (!IsValid(Ingredient) || !Ingredient->ActorHasTag(CSProductTags::TemporaryProduct))
			{
				continue;
			}
			for (ACSProductPickupActor* Product : Products)
			{
				for (const FName& Tag : Ingredient->Tags)
				{
					Product->Tags.AddUnique(Tag);
				}
			}
		}
	}
	else if (!SpawnFailureTrash(Evaluation.Outcome, ResultName))
	{
		UE_LOG(LogTemp, Error, TEXT("[제조] 실패 쓰레기 스폰에 실패해서 재료를 그대로 둡니다."));
		WaitForRetry();
		return;
	}

	// 결과물 스폰이 성공한 뒤에 기록한다. 앞에서 하면 재시도할 때 한 번 더 쌓인다
	FCSCraftAttemptRecord Attempt;
	TArray<FName> IngredientNames;
	IngredientCounts.GetKeys(IngredientNames);
	if (const FCSCraftRecipeRow* Recipe = Crafting->FindRecipeByRowName(Evaluation.RecipeRowName))
	{
		// 레시피 표의 재료 순서로 적어야 도감에서 조리법 줄과 같은 순서로 읽힌다
		IngredientNames = { Recipe->IngredientA, Recipe->IngredientB };
	}
	else
	{
		IngredientNames.Sort(FNameLexicalLess());
	}
	if (IngredientNames.Num() == 2)
	{
		Attempt.IngredientA = IngredientNames[0];
		Attempt.IngredientACount = IngredientCounts.FindRef(IngredientNames[0]);
		Attempt.IngredientB = IngredientNames[1];
		Attempt.IngredientBCount = IngredientCounts.FindRef(IngredientNames[1]);
	}
	Attempt.Heat = CurrentHeat;
	Attempt.StirCount = StirCount;
	Attempt.PopHits = PopHits;
	Attempt.PopCount = CurrentPopCount;
	Attempt.Outcome = Evaluation.Outcome;
	Attempt.RecipeRowName = Evaluation.RecipeRowName;
	Attempt.ResultProduct = bMadeProduct ? Evaluation.ResultProduct : NAME_None;
	Crafting->RecordManualCraft(Attempt);

	GetWorld()->GetTimerManager().ClearTimer(BubbleTimerHandle);

	// 결과가 생긴 뒤에 재료를 지운다. 쓰레기통을 거치지 않아서 폐기 손실에 잡히지 않는다
	for (TObjectPtr<ACSProductPickupActor>& Ingredient : IngredientSlots)
	{
		if (IsValid(Ingredient))
		{
			Ingredient->Destroy();
		}
		Ingredient = nullptr;
	}

	FName MessageRow = TEXT("ResultFailure");
	switch (Evaluation.Outcome)
	{
	case ECSCraftOutcome::GreatSuccess: MessageRow = TEXT("ResultGreatSuccess"); break;
	case ECSCraftOutcome::Success: MessageRow = TEXT("ResultSuccess"); break;
	case ECSCraftOutcome::Junk: MessageRow = TEXT("ResultJunk"); break;
	default: break;
	}
	// 쓰레기도 정상적으로 나온 결과라 경고로 띄우지 않는다
	ShowMessage(FText::Format(GetMessage(MessageRow), ResultName), false);

	OnCraftFinished(Evaluation.Outcome, Evaluation.Quantity);

	bIngredientsLocked = false;
	StirCount = 0;
	CurrentPopCount = 0;
	PopsUsed = 0;
	PopHits = 0;
	LastPoppedPeakIndex = 0;
	bFinishRetryPending = false;
	CurrentBubblePeriod = 0.0f;
	SetHeat(ECSCraftHeat::None);
	SetStep(ECSCraftStep::Idle);
	RefreshModels();
}

bool ACSCraftStationActor::SpawnResultProducts(const FCSCraftEvaluation& Evaluation, TArray<ACSProductPickupActor*>& OutProducts)
{
	// 계산대는 판매할 때 개당 원가를 DT PurchasePrice로 기록해서, 대성공 2개는 기록 원가가 실제 재료비보다 크다.
	// 지금은 원가로 이익을 보여 주는 화면이 없어서 괜찮지만, 생기면 개체별 원가를 따로 기록해야 한다
	const int32 Quantity = FMath::Clamp(Evaluation.Quantity, 1, ResultSlotCount);
	TArray<FTransform> SpawnTransforms;
	for (int32 Index = 0; Index < Quantity; ++Index)
	{
		SpawnTransforms.Add(LocationAndRotationOnly(GetResultModel(Index)));
	}
	if (!SpawnFreshProducts(Evaluation.ResultProduct, SpawnTransforms, OutProducts))
	{
		return false;
	}

	for (int32 Index = 0; Index < OutProducts.Num(); ++Index)
	{
		ResultSlots[Index] = OutProducts[Index];
		StoreProductHidden(OutProducts[Index]);
	}
	return true;
}

bool ACSCraftStationActor::SpawnFailureTrash(ECSCraftOutcome Outcome, FText& OutDisplayName)
{
	if (!TrashActorClass)
	{
		return false;
	}

	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	const FCSCraftFailureRow* FailureRow = IsValid(Crafting) ? Crafting->GetFailureRow(Outcome) : nullptr;
	if (FailureRow == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] DT_CraftFailure에 이 결과의 행이 없어 기본 쓰레기로 만듭니다."));
	}

	// [백무송님] 저장 연결: 제조 쓰레기 이름은 저장되지 않는다. 쓰레기 저장 레코드(FCSTrashRecord, CSSaveTypes.h:926-956)에
	// 이름 칸이 없고 복원(CSTrashActor.cpp:150-209)이 메시만 되돌려서, 불러오면 "쓰레기"로 보인다. 이름 칸 추가가 필요하다
	// [백무송] 저장 연결 완료
	// FCSTrashRecord에 DisplayName 칸을 추가해 쓰레기 표시 이름을 저장
	// ACSTrashActor::CaptureSaveRecord에서 현재 이름을 기록하고 RestoreSaveRecord에서 복원
	// 이름 칸이 없던 이전 세이브는 빈 값이므로 클래스 기본 이름을 유지
	const FTransform SpawnTransform = LocationAndRotationOnly(ResultModel1);
	ACSTrashActor* Trash = GetWorld()->SpawnActorDeferred<ACSTrashActor>(
		TrashActorClass, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Trash))
	{
		return false;
	}
	if (FailureRow != nullptr)
	{
		if (!FailureRow->DisplayName.IsEmpty())
		{
			Trash->SetTrashDisplayName(FailureRow->DisplayName);
		}
		Trash->SetTrashMesh(FailureRow->Mesh.LoadSynchronous());
	}
	// 손님이 흘린 쓰레기가 아니라 PrepareForCustomerDrop은 부르지 않는다
	Trash->FinishSpawning(SpawnTransform);
	if (!IsValid(Trash))
	{
		return false;
	}

	OutDisplayName = Trash->GetTrashDisplayName();
	// Apply after construction so BP defaults cannot overwrite the per-failure visual.
	// Missing registry entries retain the existing DT_CraftFailure mesh and common trash icon.
	if (const UCSCraftFailureVisualRegistry* Registry = IsValid(Crafting) ? Crafting->GetFailureVisualRegistry() : nullptr)
	{
		if (const FCSCraftFailureVisualEntry* Visual = Registry->FindVisualData(Outcome))
		{
			Trash->ApplyCraftFailureVisual(*Visual);
		}
	}
	LastFailureTrash = Trash;
	return true;
}

void ACSCraftStationActor::SetHeat(ECSCraftHeat NewHeat)
{
	if (CurrentHeat == NewHeat)
	{
		return;
	}
	CurrentHeat = NewHeat;
	OnHeatChanged(CurrentHeat);
	RefreshInfoWidget();
}

void ACSCraftStationActor::SetStep(ECSCraftStep NewStep)
{
	CraftStep = NewStep;
	if (IsValid(ActiveCloseupWidget))
	{
		ActiveCloseupWidget->SetStep(CraftStep);
	}
	RefreshInfoWidget();
}

// =====================================================================
// 솥 위 정보 창
// =====================================================================

void ACSCraftStationActor::RefreshInfoWidget()
{
	// 클로즈업 카메라는 솥을 내려다봐서 솥 위 월드 정보 창이 화면에 안 들어온다.
	// 클로즈업 위젯 안에 같은 정보 창(StationInfo)을 두고 둘 다 같은 값으로 채운다
	TArray<UCSCraftStationInfoWidget*, TInlineAllocator<2>> InfoWidgets;
	if (IsValid(InfoWidgetComponent))
	{
		if (UCSCraftStationInfoWidget* WorldInfo = Cast<UCSCraftStationInfoWidget>(InfoWidgetComponent->GetUserWidgetObject()))
		{
			InfoWidgets.Add(WorldInfo);
		}
	}
	if (IsValid(ActiveCloseupWidget))
	{
		if (UCSCraftStationInfoWidget* CloseupInfo = Cast<UCSCraftStationInfoWidget>(ActiveCloseupWidget->GetWidgetFromName(TEXT("StationInfo"))))
		{
			InfoWidgets.Add(CloseupInfo);
		}
	}
	if (InfoWidgets.IsEmpty())
	{
		return;
	}

	// 넣은 순서대로 A, B 칸에 보여 준다
	TArray<FName> Order;
	TMap<FName, int32> Counts;
	TMap<FName, const ACSProductPickupActor*> Samples;
	for (const ACSProductPickupActor* Product : IngredientSlots)
	{
		if (!IsValid(Product))
		{
			continue;
		}
		const FName ProductId = Product->GetProductId();
		if (!Counts.Contains(ProductId))
		{
			Order.Add(ProductId);
			Samples.Add(ProductId, Product);
		}
		++Counts.FindOrAdd(ProductId);
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		UTexture2D* Icon = nullptr;
		int32 Count = 0;
		if (Order.IsValidIndex(Index))
		{
			Count = Counts[Order[Index]];
			const ACSProductPickupActor* Sample = Samples[Order[Index]];
			if (const UCSProductVisualRegistry* Registry = ResolveVisualRegistry(Sample))
			{
				if (const FCSProductVisualData* VisualData = Registry->FindVisualData(Order[Index]))
				{
					Icon = VisualData->ProductIcon.LoadSynchronous();
				}
			}
		}
		for (UCSCraftStationInfoWidget* InfoWidget : InfoWidgets)
		{
			InfoWidget->SetIngredient(Index, Icon, Count);
		}
	}

	FText ResultName;
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	if (Order.Num() == 2 && IsValid(Crafting))
	{
		ResultName = GetMessage(TEXT("UnknownResult"));
		if (Crafting->IsRecipeDiscovered(Order[0], Order[1]))
		{
			const FCSCraftRecipeRow* Recipe = Crafting->FindRecipe(Order[0], Order[1]);
			const UDataTable* ProductTable = Crafting->GetProductTable();
			const FCSProductDataRow* ProductRow = Recipe && IsValid(ProductTable)
				? ProductTable->FindRow<FCSProductDataRow>(Recipe->ResultProduct, TEXT(""), false)
				: nullptr;
			if (ProductRow != nullptr)
			{
				ResultName = ProductRow->DisplayName;
			}
		}
	}
	for (UCSCraftStationInfoWidget* InfoWidget : InfoWidgets)
	{
		InfoWidget->SetResultName(ResultName);
	}

	FText Status;
	switch (CraftStep)
	{
	case ECSCraftStep::Heat:
	{
		const FName HeatRow = CurrentHeat == ECSCraftHeat::High ? FName(TEXT("HeatHigh"))
			: CurrentHeat == ECSCraftHeat::Medium ? FName(TEXT("HeatMedium"))
			: FName(TEXT("HeatLow"));
		Status = FText::Format(GetMessage(TEXT("StatusHeat")), GetMessage(HeatRow));
		break;
	}
	case ECSCraftStep::Stir:
		Status = FText::Format(GetMessage(TEXT("StatusStir")), FText::AsNumber(StirCount), FText::AsNumber(CurrentPopCount));
		break;
	case ECSCraftStep::Pop:
		// 맞힌 횟수는 보여 주지 않는다. 성공 기준이 드러난다
		Status = FText::Format(GetMessage(TEXT("StatusPop")), FText::AsNumber(FMath::Max(0, CurrentPopCount - PopsUsed)));
		break;
	default:
		Status = GetMessage(TEXT("StatusIdle"));
		break;
	}
	for (UCSCraftStationInfoWidget* InfoWidget : InfoWidgets)
	{
		InfoWidget->SetStatus(Status);
	}
}
