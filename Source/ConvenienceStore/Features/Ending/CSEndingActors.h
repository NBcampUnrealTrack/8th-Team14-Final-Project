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
struct FCSEndingSaveRecord;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnEndingSubmissionChanged);

// 사용 효과 없이 기존 도구의 줍기와 퀵바 보관 기능을 재사용합니다.
UCLASS()
class CONVENIENCESTORE_API ACSEndingItemActor : public ACSUsableToolActor
{
	GENERATED_BODY()
public:
	ACSEndingItemActor();
	virtual void PrepareForPickupPhysics() override;
	virtual void EnableWorldDropPhysics() override;
	void LaunchFromGacha(const FVector& Impulse);
	virtual void OnEquipped(ACSPlayerCharacter* NewOwner) override;
	void InitializeItem(const FCSEndingItemDefinition& Definition, ACSEndingManager* Manager);
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") FName EndingItemId;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") TObjectPtr<ACSEndingManager> IssuingManager;

	// 아이템 씬 루트의 월드 트랜스폼을 반환
	// 바닥 물리 중 메시가 루트로 바뀐 상태에서도 생성 기준 위치를 저장하는 데 사용
	FTransform GetSaveTransform() const;
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
	// 처음 집은 배출 상품은 이후 내려놓더라도 다음 구매를 막지 않습니다.
	void NotifyItemPickedUp(ACSEndingItemActor* Item);
	void SubmitOrActivate(ACSPlayerCharacter* Player);

	// ----- 세이브 / 로드 -----
	// 자금 복원 후 활성화 조건이 다시 충족되어 엔딩이 재발동하는 상황을 방지

	// 구매 순번·제출 목록·엔딩 시작 여부를 레코드에 기록
	void CaptureSaveRecord(FCSEndingSaveRecord& OutRecord) const;

	// 레코드로 진행 상태를 복원하고 전시대에 제출 현황 변경을 알림
	void RestoreSaveRecord(const FCSEndingSaveRecord& InRecord);

	// 식별자에 해당하는 엔딩 아이템을 생성하고 이 관리자의 발급 정보를 설정해 반환
	// 결제와 구매 순번 증가는 처리하지 않으며 정의가 없으면 nullptr을 반환
	// 생성 배율은 구매 배출과 같이 클래스 루트 배율을 곱해 적용
	ACSEndingItemActor* SpawnRestoredItem(FName ItemId, const FTransform& SpawnTransform);

	// 구매 후 아직 줍지 않은 아이템인지 확인
	bool IsPendingPickup(const ACSEndingItemActor* Item) const { return IsValid(Item) && PendingPickup == Item; }

	// 복원한 아이템을 줍기 대기 아이템으로 설정
	void SetPendingPickup(ACSEndingItemActor* Item) { PendingPickup = Item; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ending") TObjectPtr<UCSEndingData> EndingData;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") int32 NextPurchaseIndex = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") TSet<FName> SubmittedItemIds;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ending") bool bEndingStarted = false;
	// 제출 확정 후 전시대에 변경 사실을 알립니다.
	UPROPERTY(BlueprintAssignable, Category = "Ending") FCSOnEndingSubmissionChanged OnSubmissionChanged;
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
