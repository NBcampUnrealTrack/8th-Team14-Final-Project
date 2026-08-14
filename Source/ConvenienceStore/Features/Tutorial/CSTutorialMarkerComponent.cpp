#include "Features/Tutorial/CSTutorialMarkerComponent.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UI/Tutorial/CSTutorialMarkerWidgetBase.h"

UCSTutorialMarkerComponent::UCSTutorialMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// 항상 카메라를 정면으로 마주보게 그림
	SetWidgetSpace(EWidgetSpace::Screen);
	SetDrawAtDesiredSize(true);

	// 켜지기 전까지는 안 보이게 시작
	SetVisibility(false);
}

void UCSTutorialMarkerComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 마커가 꺼져 있으면 거리 계산 자체를 하지 않음
	if (!bIsMarkerActive)
	{
		return;
	}

	const float Alpha = ComputeDistanceAlpha();

	if (UUserWidget* CurrentWidgetInstance = GetWidget())
	{
		CurrentWidgetInstance->SetRenderOpacity(Alpha);
	}
}

void UCSTutorialMarkerComponent::ShowMarker(const FText& Label)
{
	bIsMarkerActive = true;
	SetVisibility(true);

	if (UCSTutorialMarkerWidgetBase* MarkerWidget = Cast<UCSTutorialMarkerWidgetBase>(GetWidget()))
	{
		MarkerWidget->SetMarkerLabel(Label);
	}
}

void UCSTutorialMarkerComponent::HideMarker()
{
	bIsMarkerActive = false;
	SetVisibility(false);
}

float UCSTutorialMarkerComponent::ComputeDistanceAlpha() const
{
	const APlayerController* PlayerController =
		GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* PlayerPawn = IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;

	if (!IsValid(PlayerPawn))
	{
		return 1.0f;
	}

	const float Distance = FVector::Dist(GetComponentLocation(), PlayerPawn->GetActorLocation());

	if (FadeEndDistance <= FadeStartDistance)
	{
		// 잘못된 설정(끝 거리가 시작 거리보다 짧음)이면 페이드 없이 항상 선명하게 둠
		return 1.0f;
	}

	const float NormalizedDistance = FMath::Clamp(
		(Distance - FadeStartDistance) / (FadeEndDistance - FadeStartDistance), 0.0f, 1.0f);

	return 1.0f - NormalizedDistance;
}
