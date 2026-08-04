#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CSPlayerCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USceneComponent;
struct FInputActionValue;

UCLASS()
class CONVENIENCESTORE_API ACSPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// 플레이어 캐릭터의 기본 컴포넌트와 이동 값을 설정합니다.
	ACSPlayerCharacter();

	// 상호작용한 물체를 플레이어 손 위치에 부착합니다.
	bool TryPickUp(AActor* PickupActor);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	
protected:
	// 플레이어 시점을 제공하는 1인칭 카메라입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	// 손에 든 물체를 카메라 앞에 부착하는 기준점입니다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> HeldItemAnchor;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<AActor> HeldActor;
	
	// 에디터에서 만든 입력 에셋을 연결합니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerMappingContext;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> DropAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleClockAction;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float InteractionDistance = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float DropDistance = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction",
	meta = (ClampMin = "50.0"))
	float DropTraceDepth = 400.0f;

	
private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void TryInteract();
	void DropHeldItem();
	void ToggleClock();
	void UpdatePlacementPreview();
	
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float WalkSpeed = 350.0f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float SprintSpeed = 600.0f;
};
