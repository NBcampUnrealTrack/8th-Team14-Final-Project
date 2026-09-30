#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSCraftTypes.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSCraftStationActor.generated.h"

class ACSDeliveryCrateActor;
class ACSProductPickupActor;
class ACSStorePlayerController;
class ACSTrashActor;
class APlayerController;
class UBoxComponent;
class UCameraComponent;
class UCSCraftAutoPanelWidget;
class UCSCraftCloseupWidget;
class UCSCraftingSubsystem;
class UCSCraftStationInfoWidget;
class UCSCraftSummonListWidget;
class UCSProductVisualRegistry;
class UInputAction;
class UInputMappingContext;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UTexture2D;
class UWidgetComponent;

// 창고에 있는 상품 한 종류의 합계. 소환 목록 한 줄이 된다
struct FCSCraftStorageStock
{
	FName ProductId;
	FText DisplayName;
	TSoftObjectPtr<UTexture2D> Icon;
	int32 Count = 0;
};

// 자동 제조 목록 한 줄. MaxSets가 0이면 재료가 모자라 고를 수 없다
struct FCSCraftAutoOption
{
	FName RecipeRowName;
	FText ResultName;
	TSoftObjectPtr<UTexture2D> ResultIcon;
	FText IngredientAName;
	int32 IngredientANeed = 0;
	int32 IngredientAOwned = 0;
	FText IngredientBName;
	int32 IngredientBNeed = 0;
	int32 IngredientBOwned = 0;
	int32 MaxSets = 0;
};

// 마법 제조대(솥)
// 재료 상품은 부착하지 않고 숨겨 두고, 솥에는 모형 메시만 보여 준다.
// 제조대에 부착된 상품은 CanPackProduct에 걸려 E로 다시 꺼낼 수 없다.
// ACSShelfActor를 상속하면 손님 진열대 목록에 들어가 손님이 솥을 둘러보고 진열대 추첨 순서도 밀린다
UCLASS()
class CONVENIENCESTORE_API ACSCraftStationActor : public AActor,
	public ICSShelfPlaceable,
	public ICSInteractable
{
	GENERATED_BODY()

public:
	static constexpr int32 IngredientSlotCount = 4;
	static constexpr int32 ResultSlotCount = 2;
	// 소환과 자동 제조 결과가 포장 상자 하나에 들어가야 한다
	static constexpr int32 MaxCircleQuantity = 6;
	// 종류마다 상자 하나씩 놓는다. 마법진 자리(3×2)보다 많으면 상자끼리 겹친다
	static constexpr int32 MaxSummonKinds = 6;

	ACSCraftStationActor();

	// 상자를 들고 Q를 누르면 플레이어 쪽에서 한 개씩 꺼내 TryPlaceItem으로 넘겨 준다
	virtual bool CanPlaceItem_Implementation(AActor* ItemActor) const override;
	virtual bool GetPlacementPreview_Implementation(
		AActor* ItemActor, FVector PlacementHint, FTransform& OutSlotWorldTransform) const override;
	virtual ECSShelfPlacementPreviewState GetPlacementPreviewState_Implementation(
		AActor* ItemActor, FVector PlacementHint, FTransform& OutSlotWorldTransform) const override;
	virtual bool TryPlaceItem_Implementation(AActor* ItemActor, FVector PlacementHint) override;
	virtual void RemovePlacedItem_Implementation(AActor* ItemActor) override;
	virtual bool GetItemSlotLevel_Implementation(AActor* ItemActor, ECSShelfSlotLevel& OutSlotLevel) const override;

	// Interact에는 조준한 컴포넌트가 안 넘어와서 제조대가 직접 다시 트레이스한다
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	UFUNCTION(BlueprintPure, Category = "Crafting")
	ECSCraftStep GetCraftStep() const { return CraftStep; }

	UFUNCTION(BlueprintPure, Category = "Crafting")
	ECSCraftHeat GetCurrentHeat() const { return CurrentHeat; }

	UFUNCTION(BlueprintPure, Category = "Crafting")
	int32 GetStirCount() const { return StirCount; }

	UFUNCTION(BlueprintPure, Category = "Crafting")
	bool IsCloseupActive() const { return bCloseupActive; }

	// 소환 목록과 자동 제조 창이 부른다. 반환 문구는 창 안내 칸에 그대로 보여 준다
	TArray<FCSCraftStorageStock> GetStorageStockList() const;
	// 고른 종류를 한 번에 소환한다. 하나라도 모자라면 아무것도 꺼내지 않는다
	FText RequestSummon(const TMap<FName, int32>& Selections, bool& bOutSummoned);
	void CloseSummonList();

	TArray<FCSCraftAutoOption> GetAutoCraftOptions() const;
	// 만들었으면 창을 닫고 배너를 띄운 뒤 빈 문구를 돌려준다
	FText RequestAutoCraft(FName RecipeRowName, int32 Sets);
	void CloseAutoPanel();

	FText GetMessage(FName MessageRowName) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 연출용 이벤트. 판정은 C++에서 하므로 BP 연출을 바꿔도 결과는 그대로다
	UFUNCTION(BlueprintImplementableEvent, Category = "Crafting|Presentation")
	void OnHeatChanged(ECSCraftHeat NewHeat);

	UFUNCTION(BlueprintImplementableEvent, Category = "Crafting|Presentation")
	void OnStirred(int32 NewStirCount);

	// 거품 주기가 시작될 때마다 부른다. Period 끝에 가장 커지게 그려야 판정 시점과 맞는다
	UFUNCTION(BlueprintImplementableEvent, Category = "Crafting|Presentation")
	void OnBubbleCycle(float Period);

	// 거품은 맞든 틀리든 터진다. BubbleHiddenSeconds 동안 숨겨야 같은 최고점에서 다시 부풀어 보이지 않는다
	UFUNCTION(BlueprintImplementableEvent, Category = "Crafting|Presentation")
	void OnBubblePopResult(bool bHit, const FText& ResultText, float BubbleHiddenSeconds);

	UFUNCTION(BlueprintImplementableEvent, Category = "Crafting|Presentation")
	void OnCraftFinished(ECSCraftOutcome Outcome, int32 Quantity);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 몸통에 E를 누르면 클로즈업
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	// 솥 가장자리 재료 모형. 빈 슬롯은 숨기고 충돌도 꺼서 몸통이 조준되게 한다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> IngredientModel1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> IngredientModel2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> IngredientModel3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> IngredientModel4;

	// 솥 옆 결과 모형. 실패 쓰레기는 첫 번째 자리에 놓인다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ResultModel1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ResultModel2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> CloseupCamera;

	// 솥 위 정보 창. 이 컴포넌트의 WidgetClass에 WBP_CraftStationInfo를 넣는다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> InfoWidgetComponent;

	// 솥 옆 바닥 마법진. 조준하고 E를 누르면 창고 재료 목록이 뜬다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SummonCircleMesh;

	// 소환물과 자동 제조 결과가 놓이는 자리. 이 안에 상품이나 상자가 있으면 새로 놓지 않는다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> SummonArea;

	// 모형 메시와 정보 창 아이콘을 찾는 곳. 비어 있으면 상품 액터에 지정된 레지스트리를 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TObjectPtr<UCSProductVisualRegistry> VisualRegistry;

	// 결과 상품 스폰용 (BP_ProductBase)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TSubclassOf<ACSProductPickupActor> ProductActorClass;

	// 실패 쓰레기 스폰용 (BP_Trash)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TSubclassOf<ACSTrashActor> TrashActorClass;

	// 2개 이상을 마법진에 놓을 때 담는 포장 상자 (BP_DeliveryCrateBase)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TSubclassOf<ACSDeliveryCrateActor> PackingCrateClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftCloseupWidget> CloseupWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftSummonListWidget> SummonListWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftAutoPanelWidget> AutoPanelWidgetClass;

	// 플레이어 IMC(0)보다 높아야 A/D, 클릭, Space, E, Esc를 먼저 가져간다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputMappingContext> CloseupMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	int32 CloseupMappingPriority = 10;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputAction> HeatDownAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputAction> HeatUpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputAction> StirAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputAction> PopAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputAction> FinishAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputAction> ExitAction;

	// 클로즈업에서 자동 제조 창을 연다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|Input")
	TObjectPtr<UInputAction> AutoCraftAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Camera", meta = (ClampMin = "0.0"))
	float CameraBlendTime = 0.35f;

	// 레시피가 없는 조합일 때 쓰는 거품 주기
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Timing", meta = (ClampMin = "0.1"))
	float DefaultBubblePeriod = 2.0f;

	// 거품 최고점과의 허용 오차(초). 밸런스 전 시작값
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Timing", meta = (ClampMin = "0.0"))
	float PopToleranceSeconds = 0.08f;

	// 레시피가 없는 조합도 거품을 터트려야 끝난다. 바로 끝나면 없는 조합인 게 드러난다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Timing", meta = (ClampMin = "1"))
	int32 DefaultPopCount = 2;

	// 플레이어 E 거리(InteractionDistance 750)와 맞춘다. 짧으면 모형을 조준해도 몸통으로 잡힌다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting|Interaction", meta = (ClampMin = "0.0"))
	float InteractTraceDistance = 750.0f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	TObjectPtr<ACSProductPickupActor> IngredientSlots[4];

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	TObjectPtr<ACSProductPickupActor> ResultSlots[2];

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	bool bIngredientsLocked = false;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	ECSCraftStep CraftStep = ECSCraftStep::Idle;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	ECSCraftHeat CurrentHeat = ECSCraftHeat::None;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	int32 StirCount = 0;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	float CraftStartTimeSeconds = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	float CurrentBubblePeriod = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	int32 CurrentPopCount = 0;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	int32 PopsUsed = 0;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	int32 PopHits = 0;

	// 시작 후 몇 번째 최고점의 거품을 마지막으로 터트렸는지. 0이면 아직 없다
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	int32 LastPoppedPeakIndex = 0;

	// 결과물 스폰에 실패했을 때만 켜진다. 이때만 E로 완성을 다시 시도한다
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Crafting|Runtime")
	bool bFinishRetryPending = false;

	UPROPERTY(Transient)
	TObjectPtr<UCSCraftCloseupWidget> ActiveCloseupWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCSCraftSummonListWidget> ActiveSummonListWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCSCraftAutoPanelWidget> ActiveAutoPanelWidget;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> CloseupController;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviousViewTarget;

	UPROPERTY(Transient)
	TObjectPtr<APawn> CloseupPawn;

private:
	enum class EAimTarget : uint8 { None, Body, Ingredient, Result, SummonCircle };

	UStaticMeshComponent* GetIngredientModel(int32 SlotIndex) const;
	UStaticMeshComponent* GetResultModel(int32 SlotIndex) const;
	EAimTarget TraceAimTarget(const APlayerController* PlayerController, int32& OutSlotIndex) const;

	UCSCraftingSubsystem* GetCraftingSubsystem() const;
	ACSStorePlayerController* GetLocalStoreController() const;
	UCSProductVisualRegistry* ResolveVisualRegistry(const ACSProductPickupActor* Product) const;
	void ShowBanner(FName MessageRowName) const;

	// 받을 수 없으면 안내 문구 RowName, 받을 수 있으면 None
	FName GetPlacementRejectReason(const ACSProductPickupActor* Product) const;
	TMap<FName, int32> CountIngredients() const;
	bool HasResultProduct() const;

	void StoreProductHidden(ACSProductPickupActor* Product);
	void RestoreProductVisible(ACSProductPickupActor* Product, const USceneComponent* AtModel);
	void ApplyModelVisual(UStaticMeshComponent* Model, int32 BaseIndex, const ACSProductPickupActor* Product);
	void RefreshModels();
	bool RetrieveFromSlot(APawn* InteractingPawn, bool bResultSlot, int32 SlotIndex);

	// 창고 재료와 마법진
	TMap<FName, FCSCraftStorageStock> CollectStorageStock() const;
	int32 CountStorageStock(FName ProductId) const;
	// 액터를 파괴하지 않고 창고에서 떼어 넘긴다. 모자라면 꺼낸 만큼만 담고 false
	bool TakeFromStorage(FName ProductId, int32 Count, TArray<ACSProductPickupActor*>& OutProducts);
	bool IsSummonCircleOccupied() const;
	FVector GetCircleDropLocation(int32 Index) const;
	FVector GetCircleSlotLocation(int32 SlotIndex) const;
	// 1개는 낱개로, 2개 이상은 포장 상자 하나로 놓는다. 상자를 못 만들면 아무것도 옮기지 않고 false
	bool PlaceProductsOnCircle(const TArray<ACSProductPickupActor*>& Products, const FVector& Location);
	void ScatterProductsOnCircle(const TArray<ACSProductPickupActor*>& Products);
	bool SpawnFreshResultsOnCircle(FName ProductId, int32 Count);
	// 하나라도 실패하면 만든 것을 전부 지우고 false
	bool SpawnFreshProducts(FName ProductId, const TArray<FTransform>& SpawnTransforms, TArray<ACSProductPickupActor*>& OutProducts);
	FText GetProductDisplayName(FName ProductId) const;
	TSoftObjectPtr<UTexture2D> FindProductIcon(FName ProductId) const;

	void OpenSummonList(APlayerController* PlayerController);
	void HandleAutoCraftOpen();

	void EnterCloseup(APlayerController* PlayerController);
	void ExitCloseup(bool bImmediate);
	void RestorePawnVisibility();
	void BindCloseupInput();

	void HandleHeatDown();
	void HandleHeatUp();
	void HandleStir();
	void HandleBubblePop();
	void HandleFinish();
	void HandleExit();
	void HandleBubbleCycle();

	void TryStartCraft();
	void FinishCraft();
	bool SpawnResultProducts(const FCSCraftEvaluation& Evaluation, TArray<ACSProductPickupActor*>& OutProducts);
	bool SpawnFailureTrash(ECSCraftOutcome Outcome, FText& OutDisplayName);
	void SetHeat(ECSCraftHeat NewHeat);
	void SetStep(ECSCraftStep NewStep);
	void RefreshInfoWidget();

	// BP에 배치한 모형 위치. 레지스트리 변환을 이 위에 곱한다
	TArray<FTransform> IngredientModelBaseTransforms;
	TArray<FTransform> ResultModelBaseTransforms;

	FTimerHandle BubbleTimerHandle;
	FTimerHandle PawnVisibilityRestoreTimerHandle;
	bool bCloseupActive = false;
	bool bCloseupInputBound = false;
	bool bPawnWasHidden = false;
	bool bPawnVisibilityOverridden = false;
};
