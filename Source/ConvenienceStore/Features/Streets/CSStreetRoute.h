#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSStreetRoute.generated.h"

class USplineComponent;

// 레인 경로 생성에 필요한 출발, 도착, 몸 크기 입력값
USTRUCT()
struct FCSStreetLaneRequest
{
	GENERATED_BODY()

	// 경로의 첫 점으로 사용할 출발 지면 좌표
	// 기준선에서 떨어진 옆 거리를 이 NPC의 출발 레인으로 사용
	FVector StartGround = FVector::ZeroVector;

	// 도착 지면 좌표
	// 기준선 위 거리만 도착 거리로 사용하며, 옆 거리는 출발 레인을 유지
	FVector EndGround = FVector::ZeroVector;

	// 레인 최대 거리를 줄일 때 사용할 몸 반경
	float BodyRadius = 0.0f;

	// NavMesh 투영 시 위아래 검색 반높이
	float QueryHalfHeight = 250.0f;
};

// 거리 NPC가 따라 걸을 길의 기준선과 폭을 보관하는 레벨 배치용 액터
// 기준선을 따라 NPC별 레인만큼 옆으로 평행 이동한 경로 점을 생성
// 게임 화면에는 표시되지 않으며 충돌도 사용하지 않음
UCLASS()
class CONVENIENCESTORE_API ACSStreetRoute : public AActor
{
	GENERATED_BODY()

public:
	// 기준선 스플라인을 루트로 생성하고 Tick을 비활성화
	ACSStreetRoute();

	// 출발점과 도착점 사이를 출발 레인 그대로 이동하는 경로 점을 생성
	// 첫 점은 StartGround, 마지막 점은 도착 거리에서 출발 레인을 유지한 위치이며 성공 시 2개 이상의 점을 채움
	// 마지막 점의 NavMesh 투영에 실패하면 EndGround를 마지막 점으로 사용
	// 기준선이 유효하지 않으면 false를 반환하고 OutPoints를 비움
	bool BuildLanePath(
		const FCSStreetLaneRequest& Request,
		TArray<FVector>& OutPoints) const;

	// 월드 위치를 기준선에 투영해 기준선 위 거리와 옆 거리를 조회
	// 옆 거리는 기준선 진행 방향의 오른쪽이 양수
	// 기준선이 유효하지 않으면 false를 반환
	bool GetRouteCoordinates(
		const FVector& WorldLocation,
		float& OutDistance,
		float& OutLateral) const;

	// 기준선 위 거리에서 몸 반경을 뺀 레인 최대 거리를 반환
	float GetLaneLimitAtDistance(
		float Distance,
		float BodyRadius) const;

	// 가게 입구의 기준선 위 거리를 반환
	// 입구 마커가 있으면 마커 위치를 투영한 거리, 없으면 StorefrontDistance를 반환
	float GetStorefrontDistance() const;

	// 가게 입구 거리가 지정되어 있는지 확인
	bool HasStorefront() const;

	// 월드 위치의 옆 거리를 그 지점의 레인 최대 거리로 나눈 레인 비율을 반환
	// 기준선 진행 방향의 오른쪽이 양수이며 -1~1로 제한하고, 기준선이 유효하지 않으면 0을 반환
	float GetLaneRatio(
		const FVector& WorldLocation,
		float BodyRadius) const;

	// 현재 위치가 목적지 방향 기준으로 가게 입구 거리를 지났는지 확인
	// 입구 거리가 없거나 기준선이 유효하지 않으면 false를 반환
	bool IsPastStorefront(
		const FVector& WorldLocation,
		const FVector& Destination) const;

	// 매장에서 나온 NPC가 레인에 합류할 지면 좌표를 계산
	// 입구 거리에서 목적지 방향으로 MergeAheadDistance만큼 앞, 레인 비율만큼 옆으로 이동한 위치를 사용
	// 입구 거리가 없거나 NavMesh 투영에 실패하면 false를 반환
	bool GetMergeLocation(
		float LaneRatio,
		float BodyRadius,
		const FVector& Destination,
		FVector& OutLocation) const;

	// 기준선 스플라인을 반환
	USplineComponent* GetSpline() const { return Spline; }

protected:
	// 길 기준선
	// 점별 Scale.Y를 RoadHalfWidth에 곱해 구간별 폭으로 사용
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Street")
	TObjectPtr<USplineComponent> Spline;

	// -----------------------------------------------------------------------
	// 길 폭 설정
	// -----------------------------------------------------------------------

	// 기준선에서 한쪽 가장자리까지 걸을 수 있는 거리
	// 실제 NavMesh 폭 이하로 지정
	UPROPERTY(EditAnywhere, Category = "Street|Width",
		meta = (ClampMin = "0.0", Units = "cm"))
	float RoadHalfWidth = 200.0f;

	// 기준선을 따라 경로 점을 찍는 간격
	UPROPERTY(EditAnywhere, Category = "Street|Width",
		meta = (ClampMin = "20.0", Units = "cm"))
	float SampleSpacing = 150.0f;

	// -----------------------------------------------------------------------
	// 레인 설정
	// -----------------------------------------------------------------------

	// 출발 위치의 옆 거리에서 진행 레인으로 옮겨 가는 거리
	UPROPERTY(EditAnywhere, Category = "Street|Lane",
		meta = (ClampMin = "0.0", Units = "cm"))
	float StartBlendDistance = 400.0f;

	// -----------------------------------------------------------------------
	// 가게 입구
	// -----------------------------------------------------------------------

	// 가게 입구의 기준선 위 거리
	// StorefrontMarker가 없을 때 사용하며 음수는 입구 없음을 의미
	UPROPERTY(EditAnywhere, Category = "Street|Storefront",
		meta = (Units = "cm"))
	float StorefrontDistance = -1.0f;

	// 가게 입구 위치를 표시하는 액터
	// 지정하면 이 액터 위치를 기준선에 투영한 거리를 입구 거리로 사용
	UPROPERTY(EditAnywhere, Category = "Street|Storefront")
	TObjectPtr<AActor> StorefrontMarker;

	// 매장에서 나온 NPC가 입구 거리에서 진행 방향으로 더 나아가 레인에 합류하는 거리
	// 입구 정면에서 직각으로 꺾지 않고 비스듬히 합류하도록 사용
	UPROPERTY(EditAnywhere, Category = "Street|Storefront",
		meta = (ClampMin = "0.0", Units = "cm"))
	float MergeAheadDistance = 250.0f;

private:
	// 기준선 위 거리의 수평 진행 방향 기준 오른쪽 벡터를 반환
	// 스플라인 롤 값과 관계없이 지면과 평행한 방향을 사용
	FVector GetFlatRightAtDistance(float Distance) const;

	// 기준선 위 거리와 옆 거리로 월드 좌표를 계산
	FVector GetLaneLocation(
		float Distance,
		float Lateral) const;

	// 레인 좌표를 NavMesh에 투영
	// 실패하면 옆 거리를 절반, 0으로 줄여 재시도하고 모두 실패하면 false를 반환
	bool ProjectLanePoint(
		float Distance,
		float Lateral,
		const FVector& QueryExtent,
		FVector& OutLocation) const;
};
