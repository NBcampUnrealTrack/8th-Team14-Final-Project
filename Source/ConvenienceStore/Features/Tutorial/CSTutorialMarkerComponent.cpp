#include "Features/Tutorial/CSTutorialMarkerComponent.h"

#include "Blueprint/UserWidget.h"
#include "Components/PrimitiveComponent.h"
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

	// 이 컴포넌트는 순수 시각 표시(라벨/아웃라인 트리거)용이라 콜리전이 필요 없고,
	// 콜리전이 남아있으면 상호작용 라인트레이스를 가로막아 그 아래 메시(CrateMesh 등)를
	// 못 맞히게 만드는 문제가 생길 수 있어 명시적으로 꺼둠
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
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

	if (bHighlightOwnerOutline)
	{
		ApplyOwnerOutline();
	}
}

void UCSTutorialMarkerComponent::HideMarker()
{
	bIsMarkerActive = false;
	SetVisibility(false);

	if (bHighlightOwnerOutline)
	{
		RestoreOwnerOutline();
	}
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

void UCSTutorialMarkerComponent::ApplyOwnerOutline()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		if (!OriginalCustomDepthStates.Contains(PrimitiveComponent))
		{
			OriginalCustomDepthStates.Add(PrimitiveComponent, PrimitiveComponent->bRenderCustomDepth);
			OriginalCustomStencilValues.Add(PrimitiveComponent, PrimitiveComponent->CustomDepthStencilValue);
		}

		PrimitiveComponent->SetRenderCustomDepth(true);
		PrimitiveComponent->SetCustomDepthStencilValue(OutlineStencilValue);
	}
}

void UCSTutorialMarkerComponent::RestoreOwnerOutline()
{
	for (const TPair<TObjectPtr<UPrimitiveComponent>, bool>& Pair : OriginalCustomDepthStates)
	{
		UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get();
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		PrimitiveComponent->SetRenderCustomDepth(Pair.Value);
		if (const int32* OriginalStencilValue = OriginalCustomStencilValues.Find(PrimitiveComponent))
		{
			PrimitiveComponent->SetCustomDepthStencilValue(*OriginalStencilValue);
		}
	}

	OriginalCustomDepthStates.Empty();
	OriginalCustomStencilValues.Empty();
}
