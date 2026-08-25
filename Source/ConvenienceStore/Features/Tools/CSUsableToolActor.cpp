#include "Features/Tools/CSUsableToolActor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Features/Player/CSPlayerCharacter.h"

ACSUsableToolActor::ACSUsableToolActor()
{
	ToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToolMesh"));
	SetRootComponent(ToolMesh);
	ToolMesh->SetSimulatePhysics(false);
	ToolMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ACSUsableToolActor::Interact_Implementation(AActor* Interactor)
{
	if (ACSPlayerCharacter* PlayerCharacter = Cast<ACSPlayerCharacter>(Interactor))
	{
		PlayerCharacter->TryPickUp(this);
	}
}

FText ACSUsableToolActor::GetInteractionText_Implementation() const
{
	return FText::Format(
		NSLOCTEXT("Tool", "PickUpTool", "{0} 줍기"),
		ToolDisplayName.IsEmpty()
			? NSLOCTEXT("Tool", "DefaultToolName", "도구")
			: ToolDisplayName);
}

bool ACSUsableToolActor::UsePrimary_Implementation(AActor* User)
{
	return false;
}

void ACSUsableToolActor::OnEquipped(ACSPlayerCharacter* NewOwner)
{
	bIsEquipped = true;
	SetActorEnableCollision(false);
}

void ACSUsableToolActor::OnUnequipped()
{
	bIsEquipped = false;
	SetActorEnableCollision(true);
}

void ACSUsableToolActor::StoreInQuickbar(USceneComponent* StorageAnchor)
{
	if (!IsValid(StorageAnchor))
	{
		return;
	}

	// 퀵바 보관 도구는 월드에 보이거나 조준되지 않아야 합니다.
	PrepareForPickupPhysics();
	bIsEquipped = false;
	SetActorEnableCollision(false);
	AttachToComponent(StorageAnchor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorHiddenInGame(true);
}

void ACSUsableToolActor::PrepareForPickupPhysics()
{
	if (!IsValid(ToolMesh))
	{
		return;
	}

	if (ToolMesh->IsSimulatingPhysics())
	{
		ToolMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ToolMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}

	// 물리를 끈 뒤에도 지팡이 모델이 루트로서 현재 상대 위치를 유지합니다.
	ToolMesh->SetSimulatePhysics(false);
}

void ACSUsableToolActor::EnableWorldDropPhysics()
{
	if (!IsValid(ToolMesh) || ToolMesh->GetStaticMesh() == nullptr)
	{
		return;
	}

	// 상품·배송 상자와 동일하게 월드 충돌과 중력을 켜 자연스럽게 떨어지도록 합니다.
	ToolMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ToolMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ToolMesh->SetGenerateOverlapEvents(true);
	ToolMesh->SetSimulatePhysics(true);
}
