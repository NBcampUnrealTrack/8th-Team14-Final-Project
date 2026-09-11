#include "Features/Ordering/CSOrderWorldWidgetComponent.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "Systems/StoreFlow/CSStorePlayerController.h"
#include "UI/Ordering/CSWorldOrderWidget.h"

UCSOrderWorldWidgetComponent::UCSOrderWorldWidgetComponent()
{
	SetWidgetSpace(EWidgetSpace::World);
	SetDrawSize(FVector2D(1785.0, 1151.0));
	SetPivot(FVector2D(0.5, 1.0));
	bReceiveHardwareInput = false;
	SetWindowFocusable(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void UCSOrderWorldWidgetComponent::OnRegister()
{
	// Override the old serialized 3D input setting before registering with Slate.
	bReceiveHardwareInput = false;
	SetVisibility(false);
	SetHiddenInGame(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Super::OnRegister();
}

void UCSOrderWorldWidgetComponent::BeginPlay()
{
	Super::BeginPlay();
	SetVisibility(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UCSOrderWorldWidgetComponent::PresentOrderWidget(UCSWorldOrderWidget* OrderWidget)
{
	if (!IsValid(OrderWidget) || PresentationState != EPresentationState::Closed)
	{
		return;
	}
	ACSStorePlayerController* Player = Cast<ACSStorePlayerController>(OrderWidget->GetOwningPlayer());
	if (!IsValid(Player) || !Player->IsLocalController() || !Player->PlayerCameraManager)
	{
		return;
	}
	TInlineComponentArray<UCameraComponent*> Cameras(GetOwner());
	UCameraComponent* ViewCamera = nullptr;
	const FVector Origin = GetOwner()->GetActorLocation();
	const FVector PlayerLocation = Player->GetPawn()
		? Player->GetPawn()->GetActorLocation() : Player->PlayerCameraManager->GetCameraLocation();
	FVector ApproachDirection = (PlayerLocation - Origin).GetSafeNormal2D();
	if (ApproachDirection.IsNearlyZero())
	{
		ApproachDirection = (Player->PlayerCameraManager->GetCameraLocation() - Origin).GetSafeNormal2D();
	}
	const FName CameraNames[] = { TEXT("OrderViewCamera_Front"), TEXT("OrderViewCamera_Left"), TEXT("OrderViewCamera_Right") };
	float BestAlignment = -2.0f;
	int32 CameraCount = 0;
	for (const FName CameraName : CameraNames)
	{
		for (UCameraComponent* Camera : Cameras)
		{
			if (Camera->GetFName() != CameraName) continue;
			++CameraCount;
			const FVector CameraDirection = (Camera->GetComponentLocation() - Origin).GetSafeNormal2D();
			const float Alignment = FVector::DotProduct(ApproachDirection, CameraDirection);
			if (!CameraDirection.IsNearlyZero() && Alignment > BestAlignment)
			{
				BestAlignment = Alignment;
				ViewCamera = Camera;
			}
			break;
		}
	}
	if (!ViewCamera || CameraCount != UE_ARRAY_COUNT(CameraNames))
	{
		UE_LOG(LogTemp, Error, TEXT("%s needs OrderViewCamera_Front, OrderViewCamera_Left and OrderViewCamera_Right positioned around the terminal."), *GetNameSafe(GetOwner()));
		// The legacy Blueprint requests its lock after this call. Release it next tick.
		ViewingPlayer = Player;
		PresentedWidget = OrderWidget;
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::CompleteCameraReturn);
		return;
	}

	ViewingPlayer = Player;
	CameraManager = Player->PlayerCameraManager;
	PresentedWidget = OrderWidget;
	PreviousViewTarget = Player->GetViewTarget();
	PreviousControlRotation = Player->GetControlRotation();
	OrderCamera = ViewCamera;
	PreviouslyActiveCameras.Reset();
	for (UCameraComponent* Camera : Cameras)
	{
		if (Camera->IsActive())
		{
			PreviouslyActiveCameras.Add(Camera);
		}
		Camera->SetActive(Camera == ViewCamera);
	}
	OrderWidget->SetIsEnabled(false);
	OrderWidget->SetVisibility(ESlateVisibility::Collapsed);
	SetVisibility(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Player->SetStoreHUDHidden(this, true);
	// The terminal Blueprint applies its existing lock immediately after this call.
	PresentationState = EPresentationState::MovingIn;
	CameraBlendHandle = CameraManager->OnBlendComplete().AddUObject(this, &ThisClass::HandleCameraBlendComplete);
	Player->SetViewTargetWithBlend(GetOwner(), CameraBlendTime, VTBlend_Cubic, 0.0f, true);
	if (CameraBlendTime <= 0.0f || PreviousViewTarget == GetOwner())
	{
		HandleCameraBlendComplete();
	}
}

void UCSOrderWorldWidgetComponent::FinishPresentation()
{
	if (PresentationState != EPresentationState::Open)
	{
		return;
	}
	SetVisibility(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (PresentedWidget) PresentedWidget->SetVisibility(ESlateVisibility::Collapsed);
	PresentationState = EPresentationState::MovingOut;
	if (ACSStorePlayerController* Player = ViewingPlayer.Get())
	{
		AActor* ReturnTarget = PreviousViewTarget.Get();
		if (!ReturnTarget)
		{
			ReturnTarget = Player->GetPawn();
		}
		Player->SetControlRotation(PreviousControlRotation);
		if (ReturnTarget)
		{
			PreviousViewTarget = ReturnTarget;
			Player->SetViewTargetWithBlend(ReturnTarget, CameraBlendTime, VTBlend_Cubic, 0.0f, true);
			if (CameraBlendTime <= 0.0f || ReturnTarget == GetOwner())
			{
				CompleteCameraReturn();
			}
			return;
		}
	}
	CompleteCameraReturn();
}

void UCSOrderWorldWidgetComponent::HandleCameraBlendComplete()
{
	if (!ViewingPlayer.IsValid())
	{
		CancelPresentation();
		return;
	}
	if (PresentationState == EPresentationState::MovingIn && ViewingPlayer->GetViewTarget() == GetOwner())
	{
		PresentationState = EPresentationState::Open;
		// Render directly at viewport resolution; never attach the menu to the 3D component.
		if (!PresentedWidget->IsInViewport()) PresentedWidget->AddToViewport(1100);
		PresentedWidget->OpenInWorld(this);
	}
	else if (PresentationState == EPresentationState::MovingOut && ViewingPlayer->GetViewTarget() == PreviousViewTarget.Get())
	{
		CompleteCameraReturn();
	}
}

void UCSOrderWorldWidgetComponent::RemoveCameraBlendListener()
{
	if (CameraManager.IsValid())
	{
		CameraManager->OnBlendComplete().Remove(CameraBlendHandle);
	}
	CameraBlendHandle.Reset();
}

void UCSOrderWorldWidgetComponent::RestoreCameraActivation()
{
	if (OrderCamera.IsValid())
	{
		OrderCamera->SetActive(false);
	}
	for (const TWeakObjectPtr<UCameraComponent>& Camera : PreviouslyActiveCameras)
	{
		if (Camera.IsValid())
		{
			Camera->SetActive(true);
		}
	}
	PreviouslyActiveCameras.Reset();
}

void UCSOrderWorldWidgetComponent::CompleteCameraReturn()
{
	RemoveCameraBlendListener();
	RestoreCameraActivation();
	PresentationState = EPresentationState::Closed;
	if (ViewingPlayer.IsValid()) ViewingPlayer->SetStoreHUDHidden(this, false);
	if (IsValid(PresentedWidget))
	{
		// Existing Blueprint cleanup resets both carts and releases its input lock here.
		PresentedWidget->CompleteCloseAfterCameraReturn();
	}
	PresentedWidget = nullptr;
	ViewingPlayer.Reset();
	PreviousViewTarget.Reset();
}

void UCSOrderWorldWidgetComponent::CancelPresentation()
{
	RemoveCameraBlendListener();
	SetVisibility(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (ACSStorePlayerController* Player = ViewingPlayer.Get())
	{
		if (PresentationState != EPresentationState::Closed)
		{
			Player->SetControlRotation(PreviousControlRotation);
			AActor* ReturnTarget = PreviousViewTarget.Get();
			if (!ReturnTarget) ReturnTarget = Player->GetPawn();
			if (ReturnTarget) Player->SetViewTarget(ReturnTarget);
		}
		if (IsValid(PresentedWidget)) Player->SetGameplayUIInputLock(PresentedWidget, false);
		Player->SetStoreHUDHidden(this, false);
	}
	RestoreCameraActivation();
	PresentationState = EPresentationState::Closed;
	UCSWorldOrderWidget* WidgetToRemove = PresentedWidget;
	PresentedWidget = nullptr;
	ViewingPlayer.Reset();
	if (IsValid(WidgetToRemove) && WidgetToRemove->IsInViewport()) WidgetToRemove->RemoveFromParent();
}

void UCSOrderWorldWidgetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
	CancelPresentation();
	Super::EndPlay(EndPlayReason);
}
