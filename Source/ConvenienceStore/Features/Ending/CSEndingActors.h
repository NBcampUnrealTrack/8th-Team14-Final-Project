#pragma once

#include "CoreMinimal.h"
#include "Features/Tools/CSUsableToolActor.h"
#include "CSEndingActors.generated.h"

class UCSEndingData;
class UUserWidget;
class UBoxComponent;
class USceneComponent;
class ACSPlayerCharacter;
class ACSEndingManager;
struct FCSEndingItemDefinition;

// 사용 효과 없이 기존 도구의 줍기와 퀵바 보관 기능을 재사용합니다.
UCLASS()
class CONVENIENCESTORE_API ACSEndingItemActor : public ACSUsableToolActor
{
	GENERATED_BODY()
public:
	ACSEndingItemActor();
	virtual void PrepareForPickupPhysics() override;
	virtual void EnableWorldDropPhysics() override;
	void InitializeItem(const FCSEndingItemDefinition& Definition, ACSEndingManager* Manager);
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") FName EndingItemId;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") TObjectPtr<ACSEndingManager> IssuingManager;
protected:
	// 메시의 상대 위치·회전·크기를 BP에서 독립적으로 편집할 수 있게 합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<USceneComponent> ItemSceneRoot;
private:
	// 물리 시뮬레이션으로 분리된 메시를 다시 주울 때 원래 상대 트랜스폼을 복원합니다.
	FTransform StoredMeshRelativeTransform = FTransform::Identity;
};

// 레벨에 하나 배치하고 구매처와 마법사가 같은 관리자를 참조하도록 설정합니다.
UCLASS()
class CONVENIENCESTORE_API ACSEndingManager : public AActor
{
	GENERATED_BODY()
public:
	ACSEndingManager();
	const UCSEndingData* GetEndingData() const;
	FText GetPurchasePrompt() const;
	FText GetWizardPrompt() const;
	void Purchase(ACSPlayerCharacter* Player, const FTransform& SpawnTransform);
	void SubmitOrActivate(ACSPlayerCharacter* Player);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ending") TObjectPtr<UCSEndingData> EndingData;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") int32 NextPurchaseIndex = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") TSet<FName> SubmittedItemIds;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") bool bEndingStarted = false;
private:
	// 배출된 물건을 집기 전에는 다음 상품이 겹쳐 나오지 않게 합니다.
	UPROPERTY(Transient) TObjectPtr<ACSEndingItemActor> PendingPickup;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ActiveEndingWidget;
	bool bTransactionInProgress = false;
};

// 외형은 블루프린트에서 추가하며, 기본 박스가 조준 판정을 담당합니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API ACSEndingInteractionActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()
public:
	ACSEndingInteractionActor();
	// BP 기본값에서 관리자 BP 클래스를 선택하면 시작 시 레벨의 해당 관리자를 찾습니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ending") TSubclassOf<ACSEndingManager> EndingManagerClass;
	// 특정 레벨 인스턴스를 지정하거나 BP에서 직접 연결하면 자동 검색보다 우선합니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Ending") TObjectPtr<ACSEndingManager> EndingManager;
protected:
	virtual void BeginPlay() override;
	// 박스를 루트에서 분리하여 BP 에디터에서 상대 트랜스폼을 편집할 수 있게 합니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ending") TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ending") TObjectPtr<UBoxComponent> InteractionBox;
};

UCLASS()
class CONVENIENCESTORE_API ACSEndingVendorActor : public ACSEndingInteractionActor
{
	GENERATED_BODY()
public:
	ACSEndingVendorActor();
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ending") TObjectPtr<USceneComponent> ItemSpawnPoint;
};

UCLASS()
class CONVENIENCESTORE_API ACSEndingWizardActor : public ACSEndingInteractionActor
{
	GENERATED_BODY()
public:
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
};
