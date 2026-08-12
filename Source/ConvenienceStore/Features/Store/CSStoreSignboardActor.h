#pragma once

#include "CoreMinimal.h"
#include "Features/Interaction/CSInteractable.h"
#include "GameFramework/Actor.h"
#include "CSStoreSignboardActor.generated.h"

class UStaticMeshComponent;
class UCSStoreBusinessConfirmWidget;
class ACSStoreGameState;

// 입간판 상호작용으로 영업 시작·종료를 요청하는 액터입니다.
UCLASS()
class CONVENIENCESTORE_API ACSStoreSignboardActor : public AActor, public ICSInteractable
{
	GENERATED_BODY()

public:
	ACSStoreSignboardActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

protected:
	UFUNCTION()
	void HandleStoreOpenStateChanged(bool bNewStoreOpen);

	UFUNCTION()
	void HandleBusinessConfirmed(bool bShouldOpen);

	UFUNCTION()
	void HandleBusinessCancelled();

	void CloseConfirmWidget();
	// 영업 상태에 맞는 Open/Close 면이 입구를 향하도록 목표 회전을 설정합니다.
	void UpdateSignboardRotation(bool bSnapToTarget);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SignboardMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Store|Business")
	TSubclassOf<UCSStoreBusinessConfirmWidget> BusinessConfirmWidgetClass;

	// Close 면이 정면을 향할 때의 기본 회전을 게임 시작 시 자동으로 저장합니다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Store|Business|Signboard")
	FRotator ClosedWorldRotation;

	// Open 면을 보이게 할 때 Close 면 기준으로 더할 Yaw 회전값입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Business|Signboard")
	float OpenYawOffset = 180.0f;

	// 입간판이 Open/Close 면으로 회전하는 속도입니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Store|Business|Signboard",
		meta = (ClampMin = "0.1", UIMin = "0.1"))
	float SignboardRotationSpeed = 360.0f;

	FRotator TargetSignboardRotation;
	bool bIsSignboardRotating = false;

	UPROPERTY(Transient)
	TObjectPtr<UCSStoreBusinessConfirmWidget> ActiveConfirmWidget;

	UPROPERTY(Transient)
	TObjectPtr<ACSStoreGameState> StoreGameState;
};
