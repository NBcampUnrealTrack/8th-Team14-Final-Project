#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSCleaningFairy.generated.h"

class ACSFairyHouse;
class ACSTrashActor;
class ACSTrashCanActor;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USceneComponent;
class UNiagaraComponent;

UENUM(BlueprintType)
enum class ECSFairyState : uint8
{
	Waiting, Emerging, ToTrash, Descending, Ascending, ToBin, Aligning, AwaitingDisposal, Returning, Entering, LeavingBin
};

UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSCleaningFairy : public AActor
{
	GENERATED_BODY()
public:
	ACSCleaningFairy();
	virtual void OnConstruction(const FTransform& Transform) override;
	void Initialize(ACSFairyHouse* InHouse);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<USkeletalMeshComponent> FairyMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy") TObjectPtr<USceneComponent> CarryPoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy|Effect") TObjectPtr<UStaticMeshComponent> Orb;
	// 전용 반짝이 이펙트가 준비되면 여기에서 Niagara 시스템을 지정해 주세요.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy|Effect") TObjectPtr<UNiagaraComponent> OrbEffect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy|Movement", meta=(ClampMin="30")) float FlightHeight = 120;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy|Movement", meta=(ClampMin="10")) float MoveSpeed = 180;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy|Movement", meta=(ClampMin="10")) float VerticalSpeed = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy|Movement", meta=(ClampMin="1")) float BodyRadius = 12;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy|Movement", meta=(ClampMin="10")) float TurnSpeed = 180;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy|Effect", meta=(ClampMin="0.1")) float TransitionDuration = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy", meta=(ClampMin="0.2")) float SearchInterval = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy", meta=(ClampMin="1")) float DisposalTimeout = 4;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Fairy") ECSFairyState State = ECSFairyState::Waiting;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Fairy") FText Status;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Sparks;
	TWeakObjectPtr<ACSFairyHouse> House;
	TWeakObjectPtr<ACSTrashActor> Trash;
	TWeakObjectPtr<ACSTrashCanActor> Bin;
	TMap<TWeakObjectPtr<ACSTrashActor>, double> RetryAfter;
	TMap<TWeakObjectPtr<ACSTrashActor>, int32> MissedDrops;
	TArray<FVector> Path;
	TArray<FVector> Trail;
	int32 PathIndex = 0;
	double Deadline = 0;
	double NextSearch = 0;
	float TransitionTime = 0;
	float SparkTime = 0;
	float AlignmentYaw = 0;
	FVector PickupLocation = FVector::ZeroVector;
	FVector TransitionStart = FVector::ZeroVector;
	bool bDisposed = false;
	bool bCarrying = false;
	bool bDisposalFailed = false;
	bool FindJob();
	void SetWaitingStatus(const FText& Message);
	bool BuildPath(const FVector& Start, const FVector& End, TArray<FVector>& OutPath) const;
	bool FindBinApproach(ACSTrashCanActor* TrashCan, const FVector& Start, TArray<FVector>& OutPath, float& OutDistance) const;
	bool TurnTowards(float Yaw, float DeltaSeconds);
	void SetPath(TArray<FVector>&& NewPath, ECSFairyState NewState);
	bool FollowPath(float DeltaSeconds);
	bool MoveTowards(const FVector& Goal, float Speed, float DeltaSeconds, bool bAtBin = false);
	void ShowOrb(bool bVisible);
	void UpdateOrb(float DeltaSeconds);
	void BeginReturn();
	void ReleaseTrash(bool bRestorePickup, bool bReleaseReservation = true);
	void FinishJob(bool bFailed);
	void LeaveBin(bool bFailed);
	bool HasUsableBin() const;
	UFUNCTION() void OnDisposed(AActor* Item, int32 TotalCount);
};
