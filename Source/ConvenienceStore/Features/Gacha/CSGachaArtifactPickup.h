#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSGachaArtifactPickup.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
// [백무송]
// 픽업 저장 레코드를 함수 선언에서 참조
struct FCSArtifactPickupRecord;

// 가챠에서 배출하거나 레벨에 미리 배치할 수 있는 공통 아티팩트 획득 액터입니다.
UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSGachaArtifactPickup : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSGachaArtifactPickup();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	UFUNCTION(BlueprintCallable, Category = "Artifact")
	bool InitializeArtifact(FDataTableRowHandle InArtifactRow, int32 InQuantity = 1);
	void Launch(FVector Impulse);
	FName GetArtifactId() const { return ArtifactId; }
	UStaticMeshComponent* GetPickupMesh() const { return DisplayMesh; }

	// [백무송]
	// 저장 시점의 아티팩트 행·수량·위치를 레코드에 기록
	void CaptureSaveRecord(FCSArtifactPickupRecord& OutRecord) const;

	// [백무송]
	// 저장 시점의 메시 위치와 물리 상태를 적용
	// InitializeArtifact와 FinishSpawning 이후에 호출
	void RestoreSaveRecord(const FCSArtifactPickupRecord& InRecord);

protected:
	virtual void BeginPlay() override;
	// BP 기본값이나 배치한 인스턴스에서 테이블과 아티팩트 행을 선택해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Artifact", meta = (RowType = "/Script/ConvenienceStore.CSJournalArtifactDataRow"))
	FDataTableRowHandle ArtifactRow;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Artifact") TObjectPtr<UStaticMesh> FallbackMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UBoxComponent> InteractionBox;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gacha") TObjectPtr<UStaticMeshComponent> DisplayMesh;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gacha") FName ArtifactId;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gacha") FText ArtifactDisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Artifact", meta = (ClampMin = "1")) int32 Quantity = 1;

private:
	bool ApplyArtifactRow();
	void RegisterArtifactTable();
};
