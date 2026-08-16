#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Trash/CSTrashable.h"
#include "CSTrashActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;

// 튜토리얼 청소 단계에서 플레이어가 E로 줍는 쓰레기
UCLASS()
class CONVENIENCESTORE_API ACSTrashActor : public AActor,
	public ICSInteractable,
	public ICSTrashable
{
	GENERATED_BODY()

public:
	ACSTrashActor();

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;

	// 손에 들기 전, 물리 시뮬레이션을 끄고 정상 위치로 되돌림
	// CSPlayerCharacter::TryPickUp()이 부름
	void PrepareForPickupPhysics();

	// 바닥에 내려놓은 뒤 자연스럽게 떨어지도록 물리 시뮬레이션을 켬
	// CSPlayerCharacter::DropHeldItem()이 부름
	void EnableWorldDropPhysics();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 실제 쓰레기 모델을 끼울 자리
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TrashMesh;
};
