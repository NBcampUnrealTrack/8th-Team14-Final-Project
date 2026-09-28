#include "Features/Gacha/CSGachaArtifactPickup.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CSJournalTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Features/Player/CSPlayerCharacter.h"
#include "Systems/Journal/CSJournalSubsystem.h"
#include "Systems/Artifact/CSArtifactSubsystem.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"

ACSGachaArtifactPickup::ACSGachaArtifactPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	SetRootComponent(InteractionBox);
	InteractionBox->SetBoxExtent(FVector(30, 30, 30));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	DisplayMesh->SetupAttachment(InteractionBox);
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	DisplayMesh->SetRelativeScale3D(FVector(0.3f));
}

void ACSGachaArtifactPickup::InitializeArtifact(FName InArtifactId, int32 InQuantity)
{
	ArtifactId = InArtifactId;
	Quantity = FMath::Max(1, InQuantity);
	ArtifactDisplayName = FText::GetEmpty();
	if (const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (const UCSArtifactSubsystem* Artifacts = GameInstance->GetSubsystem<UCSArtifactSubsystem>())
		{
			FCSJournalArtifactDataRow Definition;
			if (Artifacts->GetDefinition(ArtifactId, Definition))
			{
				ArtifactDisplayName = Definition.DisplayName;
				if (Definition.WorldMesh) DisplayMesh->SetStaticMesh(Definition.WorldMesh);
			}
		}
	}
}

void ACSGachaArtifactPickup::Launch(FVector Impulse)
{
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DisplayMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DisplayMesh->SetSimulatePhysics(true);
	DisplayMesh->AddImpulse(Impulse);
}

FText ACSGachaArtifactPickup::GetInteractionText_Implementation() const
{
	// 획득 전에도 테이블에 등록된 이름을 상호작용 안내에 표시합니다.
	if (!ArtifactDisplayName.IsEmpty())
	{
		return FText::Format(NSLOCTEXT("Gacha", "TakeNamedArtifact", "{0} 획득하기"), ArtifactDisplayName);
	}
	return NSLOCTEXT("Gacha", "TakeArtifact", "아티팩트 획득하기");
}

void ACSGachaArtifactPickup::Interact_Implementation(AActor* Interactor)
{
	ACSPlayerCharacter* Player = Cast<ACSPlayerCharacter>(Interactor);
	if (!IsValid(Player) || !Player->IsLocallyControlled() || ArtifactId.IsNone() || !GetWorld())
	{
		return;
	}

	UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	UCSJournalSubsystem* Journal = GameInstance ? GameInstance->GetSubsystem<UCSJournalSubsystem>() : nullptr;
	UCSArtifactSubsystem* Artifacts = GameInstance ? GameInstance->GetSubsystem<UCSArtifactSubsystem>() : nullptr;
	if (!Journal || !Artifacts)
	{
		return;
	}

	FCSJournalArtifact Artifact;
	if (!Journal->FindArtifact(ArtifactId, Artifact))
	{
		return;
	}

	// 도감 등록이 성공한 뒤에만 월드의 보상을 없애 주세요.
	if (Artifacts->Acquire(ArtifactId, Quantity))
	{
		if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(Player->GetController()))
		{
			Controller->ShowGameplayToast(
				NSLOCTEXT("Gacha", "ArtifactAcquired", "아티팩트 획득"),
				FText::Format(
					NSLOCTEXT("Gacha", "ArtifactJournalHint", "{0} × {1} 획득\n수첩의 아티팩트 도감에서 사용할 수 있습니다."),
					Artifact.Name, FText::AsNumber(Quantity)));
		}
		Destroy();
	}
}
