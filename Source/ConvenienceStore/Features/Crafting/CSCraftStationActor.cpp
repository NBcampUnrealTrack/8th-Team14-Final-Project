#include "Features/Crafting/CSCraftStationActor.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Data/CSProductTypes.h"
#include "Data/CSProductVisualRegistry.h"
#include "Engine/DataTable.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Features/Items/CSProductPickupActor.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Features/Trash/CSTrashActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Systems/Crafting/CSCraftingSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "TimerManager.h"
#include "UI/Crafting/CSCraftCloseupWidget.h"
#include "UI/Crafting/CSCraftStationInfoWidget.h"

namespace
{
	const FName MsgTooManyKinds(TEXT("TooManyKinds"));
	const FName MsgSlotFull(TEXT("SlotFull"));
	const FName MsgIngredientLocked(TEXT("IngredientLocked"));

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
}

ACSCraftStationActor::ACSCraftStationActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SceneRoot);
	BodyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	IngredientModel1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IngredientModel1"));
	IngredientModel2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IngredientModel2"));
	IngredientModel3 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IngredientModel3"));
	IngredientModel4 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IngredientModel4"));
	ResultModel1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ResultModel1"));
	ResultModel2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ResultModel2"));
	for (UStaticMeshComponent* Model : { IngredientModel1.Get(), IngredientModel2.Get(), IngredientModel3.Get(),
		IngredientModel4.Get(), ResultModel1.Get(), ResultModel2.Get() })
	{
		SetupModelComponent(Model, SceneRoot);
	}

	CloseupCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CloseupCamera"));
	CloseupCamera->SetupAttachment(SceneRoot);
	CloseupCamera->SetRelativeLocationAndRotation(FVector(-120.0f, 0.0f, 170.0f), FRotator(-50.0f, 0.0f, 0.0f));
	// 클로즈업에 들어갈 때만 켠다
	CloseupCamera->SetAutoActivate(false);

	InfoWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("InfoWidgetComponent"));
	InfoWidgetComponent->SetupAttachment(SceneRoot);
	InfoWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 180.0f));
	InfoWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	InfoWidgetComponent->SetDrawAtDesiredSize(false);
	InfoWidgetComponent->SetDrawSize(FVector2D(400.0f, 200.0f));
	InfoWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InfoWidgetComponent->SetTwoSided(true);
}

void ACSCraftStationActor::BeginPlay()
{
	Super::BeginPlay();

	IngredientModelBaseTransforms.SetNum(IngredientSlotCount);
	for (int32 Index = 0; Index < IngredientSlotCount; ++Index)
	{
		IngredientModelBaseTransforms[Index] = GetIngredientModel(Index)->GetRelativeTransform();
	}
	ResultModelBaseTransforms.SetNum(ResultSlotCount);
	for (int32 Index = 0; Index < ResultSlotCount; ++Index)
	{
		ResultModelBaseTransforms[Index] = GetResultModel(Index)->GetRelativeTransform();
	}

	if (PowderToleranceSeconds >= DefaultBubblePeriod * 0.5f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: 가루 허용폭 %.2f초가 기본 거품 주기 %.2f초의 절반 이상이라 언제 넣어도 성공합니다."),
			*GetName(), PowderToleranceSeconds, DefaultBubblePeriod);
	}
	if (!ProductActorClass || !TrashActorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] %s: ProductActorClass / TrashActorClass를 지정해 주세요. 완성할 수 없습니다."), *GetName());
	}

	RefreshModels();
	RefreshInfoWidget();
}

void ACSCraftStationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

UStaticMeshComponent* ACSCraftStationActor::GetIngredientModel(int32 SlotIndex) const
{
	switch (SlotIndex)
	{
	case 0: return IngredientModel1;
	case 1: return IngredientModel2;
	case 2: return IngredientModel3;
	case 3: return IngredientModel4;
	default: return nullptr;
	}
}

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

void ACSCraftStationActor::ShowBanner(FName MessageRowName) const
{
	if (ACSStorePlayerController* Controller = GetLocalStoreController())
	{
		Controller->ShowNoticeBanner(GetMessage(MessageRowName));
	}
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
	for (int32 Index = 0; Index < IngredientSlotCount; ++Index)
	{
		if (HitComponent == GetIngredientModel(Index) && IsValid(IngredientSlots[Index]))
		{
			OutSlotIndex = Index;
			return EAimTarget::Ingredient;
		}
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
	return false;
}

FName ACSCraftStationActor::GetPlacementRejectReason(const ACSProductPickupActor* Product) const
{
	if (bIngredientsLocked)
	{
		return MsgIngredientLocked;
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
	for (int32 Index = 0; Index < IngredientSlotCount; ++Index)
	{
		if (!IsValid(IngredientSlots[Index]))
		{
			// 빈 슬롯 모형은 BP 배치 위치에 있다
			OutSlotWorldTransform = LocationAndRotationOnly(GetIngredientModel(Index));
			return true;
		}
	}
	return false;
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
	RefreshModels();
	RefreshInfoWidget();
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
			RestoreProductVisible(Product, GetIngredientModel(Index));
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

void ACSCraftStationActor::ApplyModelVisual(UStaticMeshComponent* Model, int32 BaseIndex, const ACSProductPickupActor* Product)
{
	const bool bResultModel = BaseIndex >= IngredientSlotCount;
	const TArray<FTransform>& Bases = bResultModel ? ResultModelBaseTransforms : IngredientModelBaseTransforms;
	const int32 LocalIndex = bResultModel ? BaseIndex - IngredientSlotCount : BaseIndex;
	const FTransform BaseTransform = Bases.IsValidIndex(LocalIndex) ? Bases[LocalIndex] : Model->GetRelativeTransform();

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
	for (int32 Index = 0; Index < IngredientSlotCount; ++Index)
	{
		ApplyModelVisual(GetIngredientModel(Index), Index, IngredientSlots[Index]);
	}
	for (int32 Index = 0; Index < ResultSlotCount; ++Index)
	{
		ApplyModelVisual(GetResultModel(Index), IngredientSlotCount + Index, ResultSlots[Index]);
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
	case EAimTarget::Ingredient:
		if (bIngredientsLocked)
		{
			ShowBanner(MsgIngredientLocked);
		}
		else
		{
			RetrieveFromSlot(InteractingPawn, false, SlotIndex);
		}
		return;
	case EAimTarget::Result:
		RetrieveFromSlot(InteractingPawn, true, SlotIndex);
		return;
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
	case EAimTarget::Ingredient:
		return GetMessage(TEXT("InteractRetrieve"));
	case EAimTarget::Result:
		return GetMessage(TEXT("InteractRetrieveResult"));
	default:
		return GetMessage(TEXT("InteractCloseup"));
	}
}

void ACSCraftStationActor::RetrieveFromSlot(APawn* InteractingPawn, bool bResultSlot, int32 SlotIndex)
{
	TObjectPtr<ACSProductPickupActor>* Slot = bResultSlot
		? (SlotIndex >= 0 && SlotIndex < ResultSlotCount ? &ResultSlots[SlotIndex] : nullptr)
		: (SlotIndex >= 0 && SlotIndex < IngredientSlotCount ? &IngredientSlots[SlotIndex] : nullptr);
	if (Slot == nullptr || !IsValid(*Slot))
	{
		return;
	}

	ACSProductPickupActor* Product = *Slot;
	*Slot = nullptr;
	RestoreProductVisible(Product, bResultSlot ? GetResultModel(SlotIndex) : GetIngredientModel(SlotIndex));

	// 빈손이면 손에 들고, 같은 상품 상자를 들고 있으면 상자에 담긴다
	ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(InteractingPawn);
	if (!IsValid(Player) || !Player->TryCollectProduct(Product))
	{
		// 못 집으면 슬롯에 도로 넣는다
		*Slot = Product;
		StoreProductHidden(Product);
	}

	RefreshModels();
	RefreshInfoWidget();
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
	}

	if (CloseupWidgetClass)
	{
		ActiveCloseupWidget = CreateWidget<UCSCraftCloseupWidget>(PlayerController, CloseupWidgetClass);
		if (IsValid(ActiveCloseupWidget))
		{
			ActiveCloseupWidget->AddToViewport(10);
			ActiveCloseupWidget->SetStep(CraftStep);
		}
	}

	bCloseupActive = true;
}

void ACSCraftStationActor::ExitCloseup(bool bImmediate)
{
	if (!bCloseupActive)
	{
		return;
	}
	bCloseupActive = false;

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
	Bind(PowderAction, &ACSCraftStationActor::HandlePowder, TEXT("PowderAction"));
	Bind(FinishAction, &ACSCraftStationActor::HandleFinish, TEXT("FinishAction"));
	Bind(ExitAction, &ACSCraftStationActor::HandleExit, TEXT("ExitAction"));
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
	if (PowderToleranceSeconds >= CurrentBubblePeriod * 0.5f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[제조] 가루 허용폭 %.2f초가 거품 주기 %.2f초의 절반 이상이라 언제 넣어도 성공합니다."),
			PowderToleranceSeconds, CurrentBubblePeriod);
	}

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

void ACSCraftStationActor::HandlePowder()
{
	if (CraftStep != ECSCraftStep::Stir)
	{
		return;
	}

	// 타이머는 프레임 단위로 늦게 불려서 판정은 경과 시간으로 한다
	// 거품 최고점은 시작 후 Period의 정수배(1배 이상)
	const float Elapsed = GetWorld()->GetTimeSeconds() - CraftStartTimeSeconds;
	const int32 PeakIndex = FMath::Max(1, FMath::RoundToInt(Elapsed / CurrentBubblePeriod));
	bPowderOnTime = FMath::Abs(Elapsed - PeakIndex * CurrentBubblePeriod) <= PowderToleranceSeconds;

	GetWorld()->GetTimerManager().ClearTimer(BubbleTimerHandle);
	OnPowderResult(bPowderOnTime);
	SetStep(ECSCraftStep::Powder);
}

void ACSCraftStationActor::HandleFinish()
{
	if (CraftStep == ECSCraftStep::Powder)
	{
		FinishCraft();
	}
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

	const FCSCraftEvaluation Evaluation = Crafting->EvaluateCraft(CountIngredients(), CurrentHeat, StirCount, bPowderOnTime);
	const bool bMadeProduct = Evaluation.Outcome == ECSCraftOutcome::GreatSuccess ||
		Evaluation.Outcome == ECSCraftOutcome::Success ||
		Evaluation.Outcome == ECSCraftOutcome::Junk;

	FText ResultName;
	if (bMadeProduct)
	{
		TArray<ACSProductPickupActor*> Products;
		if (!SpawnResultProducts(Evaluation, Products))
		{
			// 재료를 지우기 전이라 가루 단계에 머문다
			UE_LOG(LogTemp, Error, TEXT("[제조] 결과물 '%s' 스폰에 실패해서 재료를 그대로 둡니다."), *Evaluation.ResultProduct.ToString());
			return;
		}
		ResultName = Products[0]->GetProductDisplayName();
	}
	else if (!SpawnFailureTrash(Evaluation.Outcome, ResultName))
	{
		UE_LOG(LogTemp, Error, TEXT("[제조] 실패 쓰레기 스폰에 실패해서 재료를 그대로 둡니다."));
		return;
	}

	// 결과가 생긴 뒤에 재료를 지운다. 쓰레기통을 거치지 않아서 폐기 손실에 잡히지 않는다
	for (TObjectPtr<ACSProductPickupActor>& Ingredient : IngredientSlots)
	{
		if (IsValid(Ingredient))
		{
			Ingredient->Destroy();
		}
		Ingredient = nullptr;
	}

	if (bMadeProduct)
	{
		Crafting->MarkRecipeDiscovered(Evaluation.RecipeRowName);
	}

	FName MessageRow = TEXT("ResultFailure");
	switch (Evaluation.Outcome)
	{
	case ECSCraftOutcome::GreatSuccess: MessageRow = TEXT("ResultGreatSuccess"); break;
	case ECSCraftOutcome::Success: MessageRow = TEXT("ResultSuccess"); break;
	case ECSCraftOutcome::Junk: MessageRow = TEXT("ResultJunk"); break;
	default: break;
	}
	if (ACSStorePlayerController* Controller = GetLocalStoreController())
	{
		Controller->ShowNoticeBanner(FText::Format(GetMessage(MessageRow), ResultName));
	}

	OnCraftFinished(Evaluation.Outcome, Evaluation.Quantity);

	bIngredientsLocked = false;
	StirCount = 0;
	bPowderOnTime = false;
	CurrentBubblePeriod = 0.0f;
	SetHeat(ECSCraftHeat::None);
	SetStep(ECSCraftStep::Idle);
	RefreshModels();
}

bool ACSCraftStationActor::SpawnResultProducts(const FCSCraftEvaluation& Evaluation, TArray<ACSProductPickupActor*>& OutProducts)
{
	const UCSCraftingSubsystem* Crafting = GetCraftingSubsystem();
	UDataTable* ProductTable = IsValid(Crafting) ? Crafting->GetProductTable() : nullptr;
	if (!ProductActorClass || !IsValid(ProductTable) ||
		ProductTable->FindRow<FCSProductDataRow>(Evaluation.ResultProduct, TEXT(""), false) == nullptr)
	{
		return false;
	}

	FDataTableRowHandle ProductRow;
	ProductRow.DataTable = ProductTable;
	ProductRow.RowName = Evaluation.ResultProduct;

	// 계산대는 판매할 때 개당 원가를 DT PurchasePrice로 기록해서, 대성공 2개는 기록 원가가 실제 재료비보다 크다.
	// 지금은 원가로 이익을 보여 주는 화면이 없어서 괜찮지만, 생기면 개체별 원가를 따로 기록해야 한다
	const int32 Quantity = FMath::Clamp(Evaluation.Quantity, 1, ResultSlotCount);
	for (int32 Index = 0; Index < Quantity; ++Index)
	{
		const FTransform SpawnTransform = LocationAndRotationOnly(GetResultModel(Index));
		ACSProductPickupActor* Product = GetWorld()->SpawnActorDeferred<ACSProductPickupActor>(
			ProductActorClass, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		// ApplyProductRuntimeState를 안 불러야 신선도와 유통기한이 만든 시점부터 시작한다
		if (!IsValid(Product) || !Product->InitializeProduct(ProductRow))
		{
			if (IsValid(Product))
			{
				Product->Destroy();
			}
			for (ACSProductPickupActor* Spawned : OutProducts)
			{
				Spawned->Destroy();
			}
			OutProducts.Reset();
			return false;
		}
		Product->FinishSpawning(SpawnTransform);
		if (!IsValid(Product))
		{
			for (ACSProductPickupActor* Spawned : OutProducts)
			{
				Spawned->Destroy();
			}
			OutProducts.Reset();
			return false;
		}
		OutProducts.Add(Product);
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
	UCSCraftStationInfoWidget* InfoWidget = IsValid(InfoWidgetComponent)
		? Cast<UCSCraftStationInfoWidget>(InfoWidgetComponent->GetUserWidgetObject())
		: nullptr;
	if (!IsValid(InfoWidget))
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
		InfoWidget->SetIngredient(Index, Icon, Count);
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
	InfoWidget->SetResultName(ResultName);

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
		Status = FText::Format(GetMessage(TEXT("StatusStir")), FText::AsNumber(StirCount));
		break;
	case ECSCraftStep::Powder:
		Status = GetMessage(TEXT("StatusPowder"));
		break;
	default:
		Status = GetMessage(TEXT("StatusIdle"));
		break;
	}
	InfoWidget->SetStatus(Status);
}
