#include "Features/Delivery/CSMagicCircleUpgradeOffice.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Store/CSMagicCircleUpgradeWidget.h"

ACSMagicCircleUpgradeOffice::ACSMagicCircleUpgradeOffice()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	BuildingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuildingMesh"));
	BuildingMesh->SetupAttachment(SceneRoot);
	BuildingMesh->SetCollisionProfileName(TEXT("BlockAll"));
	BuildingMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	InteractionMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InteractionMesh"));
	InteractionMesh->SetupAttachment(SceneRoot);
	InteractionMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(InteractionMesh);
	InteractionCollision->SetBoxExtent(FVector(45.0f, 45.0f, 70.0f));
	InteractionCollision->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
}

void ACSMagicCircleUpgradeOffice::Interact_Implementation(AActor* Interactor)
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	ACSStorePlayerController* Controller = Pawn ? Cast<ACSStorePlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Controller) || !Controller->IsLocalController() || Controller->IsGameplayUIInputLocked()) return;
	if (IsValid(ActiveUpgradeWidget) && ActiveUpgradeWidget->IsInViewport()) return;
	UClass* WidgetClass = UpgradeWidgetClass.Get();
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MagicCircle] %s: UpgradeWidgetClass에 CSMagicCircleUpgradeWidget 기반 WBP를 지정해 주세요."), *GetName());
		return;
	}
	ActiveUpgradeWidget = CreateWidget<UCSMagicCircleUpgradeWidget>(Controller, WidgetClass);
	if (IsValid(ActiveUpgradeWidget))
	{
		ActiveUpgradeWidget->AddToViewport(20);
	}
}

FText ACSMagicCircleUpgradeOffice::GetInteractionText_Implementation() const
{
	return NSLOCTEXT("MagicCircle", "InteractUpgrade", "마법진 강화하기");
}
