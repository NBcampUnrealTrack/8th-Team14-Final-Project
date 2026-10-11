#include "Features/Audio/CSJukeboxActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Audio/CSJukeboxWidget.h"
#include "UObject/ConstructorHelpers.h"

ACSJukeboxActor::ACSJukeboxActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot")));
	JukeboxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JukeboxMesh"));
	JukeboxMesh->SetupAttachment(GetRootComponent());
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	JukeboxMesh->SetStaticMesh(Cube.Object);
	JukeboxMesh->SetRelativeLocation(FVector(0, 0, 60));
	JukeboxMesh->SetRelativeScale3D(FVector(.7, .5, 1.2));
	JukeboxMesh->SetCollisionProfileName(TEXT("BlockAll"));
	WidgetClass = UCSJukeboxWidget::StaticClass();
}

void ACSJukeboxActor::Interact_Implementation(AActor* Interactor)
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	auto* PC = Pawn ? Cast<ACSStorePlayerController>(Pawn->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController() || PC->IsGameplayUIInputLocked() || PC->IsPaused()) return;
	if (IsValid(ActiveWidget) && ActiveWidget->IsInViewport()) return;
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract)) return;
	ActiveWidget = CreateWidget<UCSJukeboxWidget>(PC, WidgetClass);
	if (ActiveWidget) ActiveWidget->AddToViewport(20);
}

FText ACSJukeboxActor::GetInteractionText_Implementation() const
{
	return NSLOCTEXT("Jukebox", "Interact", "배경음악 선택하기");
}

void ACSJukeboxActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ActiveWidget) ActiveWidget->Close();
	Super::EndPlay(Reason);
}
