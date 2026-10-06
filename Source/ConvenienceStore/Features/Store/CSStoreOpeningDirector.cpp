#include "Features/Store/CSStoreOpeningDirector.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "TimerManager.h"

ACSStoreOpeningDirector::ACSStoreOpeningDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	OverviewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("OverviewCamera"));
	SetRootComponent(OverviewCamera);
	OverviewCamera->FieldOfView = 60.0f;
}

bool ACSStoreOpeningDirector::PlayOpening(APlayerController* Controller)
{
	if (!IsValid(Controller) || OpeningController.IsValid()) return false;
	OpeningController = Controller;
	PreviousViewTarget = Controller->GetViewTarget();
	bReturningToPlayer = false;
	bOrbitPlaying = false;
	Controller->SetViewTarget(this);

	if (IsValid(OpeningSequence))
	{
		FMovieSceneSequencePlaybackSettings Settings;
		// 엔진 5.8의 종료 상태 설정으로 연출 전에 변경된 상태를 복원합니다.
		Settings.FinishCompletionStateOverride = EMovieSceneCompletionModeOverride::ForceRestoreState;
		ALevelSequenceActor* CreatedActor = nullptr;
		SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
			GetWorld(), OpeningSequence, Settings, CreatedActor);
		SequenceActor = CreatedActor;
		if (IsValid(SequencePlayer))
		{
			SequencePlayer->OnFinished.AddDynamic(this, &ThisClass::ReturnToPlayer);
			SequencePlayer->OnStop.AddDynamic(this, &ThisClass::ReturnToPlayer);
			SequencePlayer->Play();
			return true;
		}
	}

	if (bUseOrbit)
	{
		// 시작 전에 중심과 기준 방향을 저장하여 이동 중에도 같은 원호를 유지합니다.
		OrbitCenter = IsValid(OrbitFocusActor) ? OrbitFocusActor->GetActorLocation()
			: GetActorTransform().TransformPosition(OrbitFocusOffset);
		OrbitReferenceOffset = GetActorLocation() - OrbitCenter;
		const float PlacedRadius = OrbitReferenceOffset.Size2D();
		if (PlacedRadius > KINDA_SMALL_NUMBER)
		{
			const float RadiusScale = OrbitRadius > 0.0f ? OrbitRadius / PlacedRadius : 1.0f;
			OrbitReferenceOffset.X *= RadiusScale;
			OrbitReferenceOffset.Y *= RadiusScale;
			OrbitElapsed = 0.0f;
			bOrbitPlaying = true;
			UpdateOrbit(0.0f);
			SetActorTickEnabled(true);
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("[Opening] 회전 중심을 카메라와 다른 수평 위치로 지정해 주세요. 고정 전경을 재생합니다."));
	}

	// 원호 이동을 사용하지 않는 경우에는 고정 전경으로 시작하실 수 있습니다.
	GetWorldTimerManager().SetTimer(OpeningTimer, this, &ThisClass::ReturnToPlayer,
		FMath::Max(0.1f, OverviewDuration), false);
	return true;
}

void ACSStoreOpeningDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOrbitPlaying || !OpeningController.IsValid()) return;
	OrbitElapsed += DeltaSeconds;
	const float Progress = FMath::Clamp(OrbitElapsed / FMath::Max(0.1f, OverviewDuration), 0.0f, 1.0f);
	UpdateOrbit(Progress);
	if (Progress >= 1.0f) ReturnToPlayer();
}

void ACSStoreOpeningDirector::UpdateOrbit(float Progress)
{
	// 시작과 끝의 속도를 부드럽게 줄이고, 가게를 계속 바라보며 좌우로 이동합니다.
	const float Angle = FMath::Lerp(OrbitStartAngle, OrbitEndAngle, FMath::SmoothStep(0.0f, 1.0f, Progress));
	const FVector Location = OrbitCenter + OrbitReferenceOffset.RotateAngleAxis(Angle, FVector::UpVector);
	SetActorLocationAndRotation(Location, (OrbitCenter - Location).Rotation());
}

void ACSStoreOpeningDirector::ReturnToPlayer()
{
	if (bReturningToPlayer) return;
	bReturningToPlayer = true;
	bOrbitPlaying = false;
	SetActorTickEnabled(false);
	// 마지막으로 보이던 카메라 위치를 유지하여 시퀀스 종료 시 화면이 튀지 않게 합니다.
	if (APlayerController* Controller = OpeningController.Get();
		IsValid(SequencePlayer) && Controller && IsValid(Controller->PlayerCameraManager))
	{
		SetActorLocationAndRotation(Controller->PlayerCameraManager->GetCameraLocation(),
			Controller->PlayerCameraManager->GetCameraRotation());
		OverviewCamera->SetFieldOfView(Controller->PlayerCameraManager->GetFOVAngle());
	}
	ClearSequence();
	if (APlayerController* Controller = OpeningController.Get())
	{
		// 시퀀스가 끝난 뒤에도 전경 카메라에서 플레이 시점까지 부드럽게 연결해 드립니다.
		Controller->SetViewTarget(this);
		AActor* Target = PreviousViewTarget.Get();
		if (!IsValid(Target)) Target = Controller->GetPawn();
		if (IsValid(Target))
		{
			Controller->SetViewTargetWithBlend(Target, FMath::Max(0.1f, ReturnBlendDuration), VTBlend_Cubic);
		}
	}
	GetWorldTimerManager().SetTimer(OpeningTimer, this, &ThisClass::FinishOpening,
		FMath::Max(0.1f, ReturnBlendDuration), false);
}

void ACSStoreOpeningDirector::FinishOpening()
{
	OpeningController.Reset();
	PreviousViewTarget.Reset();
	OnOpeningFinished.Broadcast();
}

void ACSStoreOpeningDirector::ClearSequence()
{
	if (IsValid(SequencePlayer))
	{
		SequencePlayer->OnFinished.RemoveDynamic(this, &ThisClass::ReturnToPlayer);
		SequencePlayer->OnStop.RemoveDynamic(this, &ThisClass::ReturnToPlayer);
		SequencePlayer->Stop();
	}
	SequencePlayer = nullptr;
	if (IsValid(SequenceActor)) SequenceActor->Destroy();
	SequenceActor = nullptr;
}

void ACSStoreOpeningDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetActorTickEnabled(false);
	GetWorldTimerManager().ClearTimer(OpeningTimer);
	ClearSequence();
	if (APlayerController* Controller = OpeningController.Get())
	{
		AActor* Target = PreviousViewTarget.Get();
		if (!IsValid(Target)) Target = Controller->GetPawn();
		if (IsValid(Target)) Controller->SetViewTarget(Target);
		FinishOpening();
	}
	Super::EndPlay(EndPlayReason);
}
