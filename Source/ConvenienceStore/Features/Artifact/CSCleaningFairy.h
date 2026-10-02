#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "CSCleaningFairy.generated.h"

class ACSFairyHouse;
class ACSTrashActor;
class ACSTrashCanActor;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USceneComponent;
class UNiagaraComponent;
class UAnimSequence;
class UBoxComponent;
class UWidgetComponent;
class UCSFairyDialogueWidget;

UENUM(BlueprintType)
enum class ECSFairyState : uint8
{
	Waiting, Emerging, ToTrash, Descending, Ascending, ToBin, Aligning, AwaitingDisposal, Returning, Entering, LeavingBin
};

UCLASS(Blueprintable)
class CONVENIENCESTORE_API ACSCleaningFairy : public AActor, public ICSInteractable
{
	GENERATED_BODY()
public:
	ACSCleaningFairy();
	virtual void OnConstruction(const FTransform& Transform) override;
	void Initialize(ACSFairyHouse* InHouse);
	void SetIntroductionActive(bool bActive);
	void UpdateIntroductionOrb(float Progress, float DeltaSeconds);
	bool IsAtHome() const { return !bIntroductionActive && State == ECSFairyState::Waiting; }
	bool CanTalkWhileWorking() const;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy|Interaction") TObjectPtr<UBoxComponent> InteractionBox;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fairy|Dialogue") TObjectPtr<UWidgetComponent> SpeechBubble;
	// CSFairyDialogueWidget을 부모로 만든 작업 중 대화용 WBP를 지정해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fairy|Dialogue") TSubclassOf<UCSFairyDialogueWidget> SpeechBubbleWidgetClass;
	// 요정 메시의 가장 높은 지점에서 말풍선까지의 월드 높이(cm)를 조절해 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fairy|Dialogue") float SpeechBubbleHeightOffset = 15.f;
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fairy|Animation") TObjectPtr<UAnimSequence> PickupTrashAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fairy|Animation") TObjectPtr<UAnimSequence> CarryTrashAnimation;
	// Authored against the existing Compact mesh, in mesh space (before component scale).
	UPROPERTY(EditDefaultsOnly, Category="Fairy|Animation") FVector PickupGripOffset = FVector(0, 72, 20);
	UPROPERTY(EditDefaultsOnly, Category="Fairy|Animation") FVector CarryGripOffset = FVector(0, 66, 26);
	UPROPERTY(EditDefaultsOnly, Category="Fairy|Animation", meta=(ClampMin="0")) float PickupAttachTime = .75f;
	UPROPERTY(EditDefaultsOnly, Category="Fairy|Animation", meta=(ClampMin="0", ClampMax="35")) float MovementTiltAngle = 20.f;
	UFUNCTION(BlueprintPure, Category="Fairy|Animation") bool IsCarryingTrash() const { return bCarrying; }

private:
	bool bIntroductionActive = false;
	UPROPERTY(Transient) TObjectPtr<UCSFairyDialogueWidget> BubbleWidget;
	double BubbleUntil = 0.;
	void UpdateInteraction();
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> HoverAnimation;
	UPROPERTY(Transient, VisibleInstanceOnly, Category="Fairy|Animation") float PickupElapsed = 0.f;
	FQuat MeshRestRotation = FQuat::Identity;
	FQuat VisualTilt = FQuat::Identity;
	FVector PreviousVisualLocation = FVector::ZeroVector;
	bool bVisualAnimationsReady = false;
	bool bPickupPlaying = false;
	bool bHorizontalFlight = false;
	void UpdateVisuals(float DeltaSeconds);
	void PlayVisualAnimation(UAnimSequence* Animation, bool bLooping, float StartTime = 0.f);
	FVector GetCarryOffset() const;
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
