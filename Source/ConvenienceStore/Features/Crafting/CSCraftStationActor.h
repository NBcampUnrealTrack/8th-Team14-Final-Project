#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CSCraftTypes.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Interaction/CSShelfPlaceable.h"
#include "CSCraftStationActor.generated.h"

class ACSProductPickupActor;
class ACSStorePlayerController;
class ACSTrashActor;
class APlayerController;
class UCameraComponent;
class UCSCraftCloseupWidget;
class UCSCraftingSubsystem;
class UCSCraftStationInfoWidget;
class UCSProductVisualRegistry;
class UInputAction;
class UInputMappingContext;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UWidgetComponent;

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

	// 모형 메시와 정보 창 아이콘을 찾는 곳. 비어 있으면 상품 액터에 지정된 레지스트리를 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TObjectPtr<UCSProductVisualRegistry> VisualRegistry;

	// 결과 상품 스폰용 (BP_ProductBase)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TSubclassOf<ACSProductPickupActor> ProductActorClass;

	// 실패 쓰레기 스폰용 (BP_Trash)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TSubclassOf<ACSTrashActor> TrashActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting|UI")
	TSubclassOf<UCSCraftCloseupWidget> CloseupWidgetClass;

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
	TObjectPtr<APlayerController> CloseupController;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviousViewTarget;

	UPROPERTY(Transient)
	TObjectPtr<APawn> CloseupPawn;

private:
	enum class EAimTarget : uint8 { None, Body, Ingredient, Result };

	UStaticMeshComponent* GetIngredientModel(int32 SlotIndex) const;
	UStaticMeshComponent* GetResultModel(int32 SlotIndex) const;
	EAimTarget TraceAimTarget(const APlayerController* PlayerController, int32& OutSlotIndex) const;

	UCSCraftingSubsystem* GetCraftingSubsystem() const;
	ACSStorePlayerController* GetLocalStoreController() const;
	UCSProductVisualRegistry* ResolveVisualRegistry(const ACSProductPickupActor* Product) const;
	FText GetMessage(FName MessageRowName) const;
	void ShowBanner(FName MessageRowName) const;

	// 받을 수 없으면 안내 문구 RowName, 받을 수 있으면 None
	FName GetPlacementRejectReason(const ACSProductPickupActor* Product) const;
	TMap<FName, int32> CountIngredients() const;
	bool HasResultProduct() const;

	void StoreProductHidden(ACSProductPickupActor* Product);
	void RestoreProductVisible(ACSProductPickupActor* Product, const USceneComponent* AtModel);
	void ApplyModelVisual(UStaticMeshComponent* Model, int32 BaseIndex, const ACSProductPickupActor* Product);
	void RefreshModels();
	void RetrieveFromSlot(APawn* InteractingPawn, bool bResultSlot, int32 SlotIndex);

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
