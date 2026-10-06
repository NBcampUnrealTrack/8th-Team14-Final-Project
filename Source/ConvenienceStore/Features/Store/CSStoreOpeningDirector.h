#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSStoreOpeningDirector.generated.h"

class UCameraComponent;
class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSOnOpeningFinished);

// 레벨에 배치한 뒤 카메라 위치를 조정하시거나 시퀀서를 지정해 주세요.
UCLASS()
class CONVENIENCESTORE_API ACSStoreOpeningDirector : public AActor
{
	GENERATED_BODY()

public:
	ACSStoreOpeningDirector();
	bool PlayOpening(APlayerController* Controller);
	UPROPERTY(BlueprintAssignable, Category = "Store|Opening")
	FCSOnOpeningFinished OnOpeningFinished;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Store|Opening")
	TObjectPtr<UCameraComponent> OverviewCamera;

	// 카메라 컷 트랙이 있는 시퀀스를 지정하시면 기본 전경 카메라 대신 재생합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening")
	TObjectPtr<ULevelSequence> OpeningSequence;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening", meta = (ClampMin = "0.1"))
	float OverviewDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening", meta = (ClampMin = "0.1"))
	float ReturnBlendDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening|Orbit")
	bool bUseOrbit = true;

	// 가게 중앙에 배치한 액터를 지정하시면 그 위치를 바라보며 회전합니다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Store|Opening|Orbit")
	TObjectPtr<AActor> OrbitFocusActor;

	// 대상 액터가 없으면 이 지점을 사용합니다. 레벨의 이동 핸들로 가게 중앙에 맞춰 주세요.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening|Orbit", meta = (MakeEditWidget = "true"))
	FVector OrbitFocusOffset = FVector(1200.0f, 0.0f, 0.0f);

	// 0으로 두시면 배치한 카메라와 회전 중심 사이의 수평 거리를 그대로 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening|Orbit", meta = (ClampMin = "0.0", Units = "cm"))
	float OrbitRadius = 0.0f;

	// 배치한 카메라의 정면 구도를 0도로 삼습니다. 두 각도를 바꾸시면 방향도 바뀝니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening|Orbit", meta = (Units = "deg"))
	float OrbitStartAngle = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Opening|Orbit", meta = (Units = "deg"))
	float OrbitEndAngle = -35.0f;

private:
	UPROPERTY(Transient) TObjectPtr<ULevelSequencePlayer> SequencePlayer;
	UPROPERTY(Transient) TObjectPtr<ALevelSequenceActor> SequenceActor;
	TWeakObjectPtr<APlayerController> OpeningController;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	FTimerHandle OpeningTimer;
	bool bReturningToPlayer = false;
	bool bOrbitPlaying = false;
	FVector OrbitCenter = FVector::ZeroVector;
	FVector OrbitReferenceOffset = FVector::ZeroVector;
	float OrbitElapsed = 0.0f;
	void UpdateOrbit(float Progress);
	UFUNCTION() void ReturnToPlayer();
	void FinishOpening();
	void ClearSequence();
};
