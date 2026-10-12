#include "Features/Tutorial/CSTutorialTriggerVolume.h"

#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "Systems/Tutorial/CSTutorialSubsystem.h"

ACSTutorialTriggerVolume::ACSTutorialTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);

	// 시각적으로는 아무것도 안 보이는 순수 판정용 볼륨이라 Pawn만 감지하면 충분
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetBoxExtent(FVector(100.0f, 100.0f, 100.0f));

	TriggerBox->OnComponentBeginOverlap.AddDynamic(
		this, &ACSTutorialTriggerVolume::HandleBeginOverlap);
}

void ACSTutorialTriggerVolume::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	// 손님 NPC 등 다른 Pawn은 무시하고 실제로 플레이어가 조종하는 폰일 때만 반응
	const APlayerController* PlayerController =
		GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!IsValid(PlayerController) || PlayerController->GetPawn() != OtherActor)
	{
		return;
	}

	if (UCSTutorialSubsystem* TutorialSubsystem = GetWorld()->GetSubsystem<UCSTutorialSubsystem>())
	{
		TutorialSubsystem->NotifyTriggerEntered(TargetStepId, this);
	}
}
