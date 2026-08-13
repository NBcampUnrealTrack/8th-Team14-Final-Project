#pragma once

#include "CoreMinimal.h"
#include "Features/Interaction/CSInteractable.h"
#include "Features/Tools/CSUsableTool.h"
#include "GameFramework/Actor.h"
#include "CSUsableToolActor.generated.h"

class ACSPlayerCharacter;
class UStaticMeshComponent;

// 지팡이, 청소 도구처럼 집어서 주 사용 입력을 실행할 수 있는 도구의 공통 부모입니다.
UCLASS(Abstract)
class CONVENIENCESTORE_API ACSUsableToolActor : public AActor, public ICSInteractable, public ICSUsableTool
{
	GENERATED_BODY()

public:
	ACSUsableToolActor();

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionText_Implementation() const override;
	virtual bool UsePrimary_Implementation(AActor* User) override;

	// 플레이어가 도구를 들거나 내려놓을 때 충돌 상태를 갱신합니다.
	virtual void OnEquipped(ACSPlayerCharacter* NewOwner);
	virtual void OnUnequipped();

	// 도구를 들기 전에 월드 물리를 끄고 손에 부착할 수 있는 상태로 준비합니다.
	virtual void PrepareForPickupPhysics();
	// 도구를 바닥에 내려놓을 때 중력과 충돌을 포함한 월드 물리를 활성화합니다.
	virtual void EnableWorldDropPhysics();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ToolMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tool")
	FText ToolDisplayName;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tool")
	bool bIsEquipped = false;
};
