#include "Features/Store/CSStoreInteriorActor.h"
#include "Components/BoxComponent.h"

ACSStoreInteriorActor::ACSStoreInteriorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	InteriorBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteriorBox"));
	SetRootComponent(InteriorBox);

	// 순수 영역 표시용이라 충돌·물리·오버랩 이벤트에 관여하지 않는다
	InteriorBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteriorBox->SetGenerateOverlapEvents(false);
	InteriorBox->SetBoxExtent(FVector(500.0f, 500.0f, 200.0f));
}

bool ACSStoreInteriorActor::EncompassesPoint(const FVector& Location) const
{
	if (!IsValid(InteriorBox))
	{
		return false;
	}

	// 박스가 회전되어 배치되어도 정확히 판정하려고 월드 좌표를 박스 로컬 좌표로 변환해서 비교한다
	// (InverseTransformPosition이 스케일까지 나눠주므로 비교 대상은 스케일 적용 전 원본 Extent다)
	const FVector LocalPoint = InteriorBox->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = InteriorBox->GetUnscaledBoxExtent();

	return FMath::Abs(LocalPoint.X) <= Extent.X
		&& FMath::Abs(LocalPoint.Y) <= Extent.Y
		&& FMath::Abs(LocalPoint.Z) <= Extent.Z;
}
