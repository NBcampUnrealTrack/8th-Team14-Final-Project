#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Trash/CSTrashable.h"
#include "CSTrashActor.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
struct FCSTrashRecord;
struct FCSCraftFailureVisualEntry;

// 튜토리얼 청소 단계에서 플레이어가 E로 줍는 쓰레기
UCLASS()
class CONVENIENCESTORE_API ACSTrashActor : public AActor,
	public ICSInteractable,
	public ICSTrashable
{
	GENERATED_BODY()

public:
	ACSTrashActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintCallable, Category = "Trash")
	bool InitializeTrash(FDataTableRowHandle InTrashRow);

	UFUNCTION(BlueprintPure, Category = "Trash")
	FDataTableRowHandle GetTrashRow() const { return TrashRow; }
	FVector GetCleaningLocation() const;
	bool ReserveForCleaning(AActor* Cleaner);
	void ReleaseCleaningReservation(AActor* Cleaner);
	bool IsCarriedByCleaner() const;
	bool CanBeCleanedBy(AActor* Cleaner) const;

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	// 손에 들기 전, 물리 시뮬레이션을 끄고 정상 위치로 되돌림
	// CSPlayerCharacter::TryPickUp()이 부름
	void PrepareForPickupPhysics();

	// 바닥에 내려놓은 뒤 자연스럽게 떨어지도록 물리 시뮬레이션을 켬
	// CSPlayerCharacter::DropHeldItem()이 부름
	void EnableWorldDropPhysics();

	// 손에 들었을 때 메시를 안 보이게 함
	void HideMeshForPickup();

	// 내려놓을 때 메시를 다시 보이게 함
	void ShowMeshAfterDrop();

	// 손님이 통로에 쓰레기를 흘렸을 때(런타임 스폰 직후) 호출
	void PrepareForCustomerDrop();

	// 제조 실패물처럼 이름·모양이 다른 쓰레기용.
	// 스폰하는 쪽에서 FinishSpawning 전에 넣어 준다
	FText GetTrashDisplayName() const { return TrashDisplayName; }
	void SetTrashDisplayName(const FText& InDisplayName) { TrashDisplayName = InDisplayName; }
	void SetTrashMesh(UStaticMesh* InMesh);
	void ApplyCraftFailureVisual(const FCSCraftFailureVisualEntry& Visual);
	UTexture2D* GetTrashIcon() const { return TrashIcon; }

	// 현재 클래스·메시·위치·물리 상태를 세이브 레코드에 기록
	void CaptureSaveRecord(FCSTrashRecord& OutRecord) const;

	// 세이브 레코드의 메시·충돌·물리 상태를 적용
	// 레코드 트랜스폼으로 스폰한 직후 호출
	void RestoreSaveRecord(const FCSTrashRecord& InRecord);

protected:
	virtual void BeginPlay() override;

	// 선택한 제조 실패 행의 이름과 Visual Registry 외형을 함께 적용합니다. None이면 일반 쓰레기입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trash",
		meta = (RowType = "/Script/ConvenienceStore.CSCraftFailureRow"))
	FDataTableRowHandle TrashRow;

	bool ApplyTrashRow();

	UPROPERTY(Transient)
	bool bHasAppliedTrashRow = false;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TrashIcon;

	UPROPERTY(Transient)
	FTransform FailureMeshRelativeTransform = FTransform::Identity;

	UPROPERTY(Transient)
	bool bHasFailureMeshTransform = false;

	TWeakObjectPtr<AActor> CleaningReservation;
	// 줍기·퀵바·버리기 안내에 쓰는 이름. 기본값은 "쓰레기"
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trash")
	FText TrashDisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 실제 쓰레기 모델을 끼울 자리
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TrashMesh;
};
