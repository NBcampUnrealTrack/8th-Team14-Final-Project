#include "Features/Store/CSStoreClosingBellActor.h"
#include "Features/Store/CSPlayerWakePoint.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Systems/StoreFlow/CSStoreGameMode.h"
#include "Systems/StoreFlow/CSStoreGameState.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "Systems/Tutorial/CSTutorialSubsystem.h"
#include "UI/Store/CSStoreClosingBellConfirmWidget.h"
#include "UI/Store/CSStoreSleepTransitionWidget.h"
#include "UI/Store/CSWakeUpWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
ACSStoreClosingBellActor::ACSStoreClosingBellActor()
{
	BellMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BellMesh"));
	SetRootComponent(BellMesh);
	BellMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	WakeUpWidgetClass = UCSWakeUpWidget::StaticClass();
}
void ACSStoreClosingBellActor::BeginPlay() { Super::BeginPlay(); StoreGameState = GetWorld()->GetGameState<ACSStoreGameState>(); }
void ACSStoreClosingBellActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SleepTimer);
	CloseConfirmWidget();
	CloseSleepTransitionWidget();
	FinishSleep();
	Super::EndPlay(EndPlayReason);
}
void ACSStoreClosingBellActor::Interact_Implementation(AActor* Interactor)
{
	if (!IsValid(StoreGameState) || StoreGameState->CurrentPhase != ECStorePhase::ClosingFreeRoam ||
		StoreGameState->bIsDailySummaryPending || IsValid(ActiveConfirmWidget) || SleepingController.IsValid() ||
		ConfirmWidgetClass == nullptr) return;
	const APawn* Pawn = Cast<APawn>(Interactor);
	APlayerController* Controller = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Controller)) return;
	if (const ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Controller);
		StoreController && StoreController->IsSleepTransitionActive()) return;
	// 튜토리얼 3부 도중에 자면 남은 안내가 다음 날까지 이어진다
	const UWorld* World = GetWorld();
	const UCSTutorialSubsystem* Tutorial = World != nullptr ? World->GetSubsystem<UCSTutorialSubsystem>() : nullptr;
	FText SleepBlockedText;
	if (IsValid(Tutorial) && Tutorial->ShouldBlockSleep(SleepBlockedText))
	{
		if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Controller)) StoreController->ShowNoticeBanner(SleepBlockedText);
		return;
	}
	ActiveConfirmWidget = CreateWidget<UCSStoreClosingBellConfirmWidget>(Controller, ConfirmWidgetClass);
	if (!IsValid(ActiveConfirmWidget)) return;
	ActiveConfirmWidget->OnClosingBellConfirmed.AddDynamic(this, &ACSStoreClosingBellActor::HandleConfirmed);
	ActiveConfirmWidget->OnClosingBellCancelled.AddDynamic(this, &ACSStoreClosingBellActor::HandleCancelled);
	ActiveConfirmWidget->AddToViewport();
	if (ACSStorePlayerController* StoreController = Cast<ACSStorePlayerController>(Controller)) StoreController->SetGameplayUIInputLock(ActiveConfirmWidget, true);
}
void ACSStoreClosingBellActor::HandleConfirmed()
{
	ACSStorePlayerController* Controller = IsValid(ActiveConfirmWidget)
		? Cast<ACSStorePlayerController>(ActiveConfirmWidget->GetOwningPlayer()) : nullptr;
	if (!IsValid(Controller) || !IsValid(Controller->PlayerCameraManager) ||
		!IsValid(StoreGameState) || StoreGameState->CurrentPhase != ECStorePhase::ClosingFreeRoam ||
		StoreGameState->bIsDailySummaryPending || !Controller->BeginSleepTransition(this))
	{
		CloseConfirmWidget();
		return;
	}
	SleepingController = Controller;
	DayBeforeSleep = StoreGameState->CurrentBusinessDay;
	CloseConfirmWidget();
	if (ACharacter* Character = Cast<ACharacter>(Controller->GetPawn()))
	{
		Character->StopJumping();
		Character->ConsumeMovementInputVector();
		Character->GetCharacterMovement()->StopMovementImmediately();
	}
	Controller->PlayerCameraManager->StartCameraFade(0.0f, 1.0f,
		FMath::Max(0.1f, SleepFadeDuration), FLinearColor::Black, false, true);
	GetWorldTimerManager().SetTimer(SleepTimer, this,
		&ACSStoreClosingBellActor::AdvanceDayWhileBlack, FMath::Max(0.1f, SleepFadeDuration), false);
}

void ACSStoreClosingBellActor::AdvanceDayWhileBlack()
{
	if (!SleepingController.IsValid()) { FinishSleep(); return; }
	ACSStoreGameMode* Mode = GetWorld()->GetAuthGameMode<ACSStoreGameMode>();
	if (!IsValid(Mode) || !Mode->RequestClosingDailySummary())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClosingBell] Sleep cancelled: closing summary was not accepted."));
		DayBeforeSleep = INDEX_NONE;
		GetWorldTimerManager().SetTimer(SleepTimer, this,
			&ACSStoreClosingBellActor::WakeAndFadeIn, FMath::Max(0.01f, SleepBlackHoldDuration), false);
		return;
	}

	ACSStorePlayerController* Controller = SleepingController.Get();
	if (SleepTransitionWidgetClass)
	{
		ActiveSleepTransitionWidget = CreateWidget<UCSStoreSleepTransitionWidget>(
			Controller, SleepTransitionWidgetClass);
	}
	if (!IsValid(ActiveSleepTransitionWidget))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClosingBell] SleepTransitionWidgetClass가 없어 기존 즉시 전환을 사용합니다."));
		Mode->CompleteDailySummary();
		GetWorldTimerManager().SetTimer(SleepTimer, this,
			&ACSStoreClosingBellActor::WakeAndFadeIn, FMath::Max(0.01f, SleepBlackHoldDuration), false);
		return;
	}

	ActiveSleepTransitionWidget->OnNextDayRequested.AddDynamic(
		this, &ThisClass::HandleSleepNextDayRequested);
	ActiveSleepTransitionWidget->OnSummaryRequested.AddDynamic(
		this, &ThisClass::HandleSleepSummaryRequested);
	ActiveSleepTransitionWidget->AddToViewport(2200);
	Controller->SetSleepTransitionWidget(ActiveSleepTransitionWidget);
	ActiveSleepTransitionWidget->SetKeyboardFocus();
}

void ACSStoreClosingBellActor::HandleSleepNextDayRequested()
{
	ACSStoreGameMode* Mode = GetWorld()->GetAuthGameMode<ACSStoreGameMode>();
	if (!IsValid(Mode) || !IsValid(StoreGameState) || !StoreGameState->bIsDailySummaryPending) return;
	CloseSleepTransitionWidget();
	Mode->CompleteDailySummary();
	GetWorldTimerManager().SetTimer(SleepTimer, this,
		&ACSStoreClosingBellActor::WakeAndFadeIn, FMath::Max(0.01f, SleepBlackHoldDuration), false);
}

void ACSStoreClosingBellActor::HandleSleepSummaryRequested()
{
	if (ACSStorePlayerController* Controller = SleepingController.Get())
	{
		Controller->ShowSleepDailySummary();
	}
}

void ACSStoreClosingBellActor::CloseSleepTransitionWidget()
{
	if (ACSStorePlayerController* Controller = SleepingController.Get())
	{
		Controller->SetSleepTransitionWidget(nullptr);
	}
	if (!IsValid(ActiveSleepTransitionWidget)) return;
	ActiveSleepTransitionWidget->OnNextDayRequested.RemoveDynamic(
		this, &ThisClass::HandleSleepNextDayRequested);
	ActiveSleepTransitionWidget->OnSummaryRequested.RemoveDynamic(
		this, &ThisClass::HandleSleepSummaryRequested);
	ActiveSleepTransitionWidget->RemoveFromParent();
	ActiveSleepTransitionWidget = nullptr;
}

void ACSStoreClosingBellActor::WakeAndFadeIn()
{
	ACSStorePlayerController* Controller = SleepingController.Get();
	if (!IsValid(Controller)) { FinishSleep(); return; }
	const bool bMorningReady = IsValid(StoreGameState) && DayBeforeSleep != INDEX_NONE &&
		StoreGameState->CurrentBusinessDay == DayBeforeSleep + 1 &&
		StoreGameState->CurrentPhase == ECStorePhase::Preparation &&
		!StoreGameState->bIsDailySummaryPending && !StoreGameState->bIsStoreOpen;
	if (bMorningReady)
	{
		APawn* Pawn = Controller->GetPawn();
		if (IsValid(WakePoint) && IsValid(Pawn))
		{
			const FRotator ViewRotation = WakePoint->GetActorRotation();
			const FRotator BodyRotation(0.0f, ViewRotation.Yaw, 0.0f);
			// TeleportTo의 충돌 검사를 유지하여 Capsule이 들어가지 않는 위치로 보내지 않습니다.
			if (Pawn->TeleportTo(WakePoint->GetActorLocation(), BodyRotation, false, false))
			{
				Controller->SetControlRotation(ViewRotation);
				Pawn->ConsumeMovementInputVector();
				if (ACharacter* Character = Cast<ACharacter>(Pawn))
				{
					Character->GetCharacterMovement()->StopMovementImmediately();
				}
			}
			else UE_LOG(LogTemp, Warning, TEXT("[ClosingBell] WakePoint is blocked; keeping player position."));
		}
		else UE_LOG(LogTemp, Warning, TEXT("[ClosingBell] No valid WakePoint/Pawn; keeping player position."));
	}
	else if (DayBeforeSleep != INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClosingBell] Next-day preparation was not completed; keeping player position."));
	}
	// 다음 날 준비가 완료된 경우에만 눈뜨기를 재생하며, 취소되면 기존 페이드로 돌아갑니다.
	if (bMorningReady && bUseEyeOpening && WakeUpWidgetClass)
	{
		ActiveWakeUpWidget = CreateWidget<UCSWakeUpWidget>(Controller, WakeUpWidgetClass);
		if (IsValid(ActiveWakeUpWidget))
		{
			ActiveWakeUpWidget->OnEyeOpeningFinished.AddDynamic(this, &ThisClass::FinishSleep);
			ActiveWakeUpWidget->AddToViewport(2400);
			// 닫힌 눈꺼풀이 화면을 덮은 뒤 카메라의 검은 페이드를 해제합니다.
			if (IsValid(Controller->PlayerCameraManager)) Controller->PlayerCameraManager->StopCameraFade();
			return;
		}
	}
	if (IsValid(Controller->PlayerCameraManager))
	{
		Controller->PlayerCameraManager->StartCameraFade(1.0f, 0.0f,
			FMath::Max(0.1f, SleepFadeDuration), FLinearColor::Black, false, false);
	}
	GetWorldTimerManager().SetTimer(SleepTimer, this,
		&ACSStoreClosingBellActor::FinishSleep, FMath::Max(0.1f, SleepFadeDuration), false);
}

void ACSStoreClosingBellActor::FinishSleep()
{
	GetWorldTimerManager().ClearTimer(SleepTimer);
	if (IsValid(ActiveWakeUpWidget))
	{
		ActiveWakeUpWidget->OnEyeOpeningFinished.RemoveDynamic(this, &ThisClass::FinishSleep);
		ActiveWakeUpWidget->RemoveFromParent();
		ActiveWakeUpWidget = nullptr;
	}
	CloseSleepTransitionWidget();
	if (ACSStorePlayerController* Controller = SleepingController.Get())
	{
		if (IsValid(Controller->PlayerCameraManager)) Controller->PlayerCameraManager->StopCameraFade();
		Controller->EndSleepTransition(this);
	}
	SleepingController.Reset();
	DayBeforeSleep = INDEX_NONE;
}
void ACSStoreClosingBellActor::HandleCancelled() { CloseConfirmWidget(); }
void ACSStoreClosingBellActor::CloseConfirmWidget()
{
	if (!IsValid(ActiveConfirmWidget)) return;
	if (ACSStorePlayerController* Controller = Cast<ACSStorePlayerController>(ActiveConfirmWidget->GetOwningPlayer())) Controller->SetGameplayUIInputLock(ActiveConfirmWidget, false);
	ActiveConfirmWidget->RemoveFromParent();
	ActiveConfirmWidget = nullptr;
}
FText ACSStoreClosingBellActor::GetInteractionText_Implementation() const
{
	if (!IsValid(StoreGameState))
	{
		return FText::GetEmpty();
	}

	if (StoreGameState->CurrentPhase == ECStorePhase::ClosingFreeRoam)
	{
		return StoreGameState->bIsDailySummaryPending
			? FText::GetEmpty()
			: NSLOCTEXT("Store", "SleepAfterClosing", "잠자기");
	}

	// 준비·영업 단계에도 마감 종의 사용 시점을 HUD 안내로 알려드립니다.
	return NSLOCTEXT("Store", "ClosingBellUnavailable", "마감 시간에 사용할 수 있습니다.");
}
