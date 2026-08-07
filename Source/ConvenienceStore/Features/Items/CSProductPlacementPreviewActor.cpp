#include "Features/Items/CSProductPlacementPreviewActor.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

ACSProductPlacementPreviewActor::ACSProductPlacementPreviewActor()
{
	// 프리뷰는 월드 상호작용이나 물리 충돌 대상이 아니므로 모든 충돌을 비활성화합니다.
	SetActorEnableCollision(false);

	if (IsValid(ProductMesh))
	{
		ProductMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ProductMesh->SetGenerateOverlapEvents(false);
		ProductMesh->SetCastShadow(false);
	}
}

bool ACSProductPlacementPreviewActor::InitializePreviewFromProduct(
	const ACSProductPickupActor* SourceProduct)
{
	if (!IsValid(SourceProduct))
	{
		return false;
	}

	// 실제 상품과 같은 DataAsset 및 fallback 설정을 복사하여 외형 차이를 방지합니다.
	VisualRegistry = SourceProduct->GetVisualRegistry();
	FallbackMesh = SourceProduct->GetFallbackMesh();
	FallbackMeshRelativeTransform = SourceProduct->GetFallbackMeshRelativeTransform();

	// 공용 상품 액터의 DataAsset 적용 로직을 재사용하여 메시와 상대 트랜스폼을 동일하게 맞춥니다.
	if (!InitializeProduct(SourceProduct->GetProductRow()))
	{
		return false;
	}

	SetPlacementValid(true);
	return true;
}

bool ACSProductPlacementPreviewActor::InitializePreviewFromProductClass(
	FDataTableRowHandle InProductRow,
	TSubclassOf<ACSProductPickupActor> InProductActorClass)
{
	if (InProductActorClass == nullptr)
	{
		return false;
	}

	const ACSProductPickupActor* ProductDefaults = InProductActorClass.GetDefaultObject();
	if (!IsValid(ProductDefaults))
	{
		return false;
	}

	VisualRegistry = ProductDefaults->GetVisualRegistry();
	FallbackMesh = ProductDefaults->GetFallbackMesh();
	FallbackMeshRelativeTransform = ProductDefaults->GetFallbackMeshRelativeTransform();

	if (!InitializeProduct(InProductRow))
	{
		return false;
	}

	SetPlacementValid(true);
	return true;
}

void ACSProductPlacementPreviewActor::SetPlacementValid(bool bIsPlacementValid)
{
	ApplyPreviewMaterial(bIsPlacementValid);
}

void ACSProductPlacementPreviewActor::Interact_Implementation(AActor* Interactor)
{
	// 프리뷰 액터는 표시 전용이므로 플레이어 상호작용을 처리하지 않습니다.
}

FText ACSProductPlacementPreviewActor::GetInteractionText_Implementation() const
{
	// 프리뷰 액터에는 표시할 상호작용 안내 문구가 없습니다.
	return FText::GetEmpty();
}

void ACSProductPlacementPreviewActor::ApplyPreviewMaterial(bool bIsPlacementValid)
{
	if (!IsValid(ProductMesh))
	{
		return;
	}

	UMaterialInterface* PreviewMaterial = bIsPlacementValid
		? ValidPlacementMaterial.Get()
		: InvalidPlacementMaterial.Get();
	if (!IsValid(PreviewMaterial))
	{
		return;
	}

	// 상품 메시의 모든 머티리얼 슬롯을 프리뷰 전용 반투명 머티리얼로 덮어씁니다.
	for (int32 MaterialIndex = 0; MaterialIndex < ProductMesh->GetNumMaterials(); ++MaterialIndex)
	{
		ProductMesh->SetMaterial(MaterialIndex, PreviewMaterial);
	}
}
