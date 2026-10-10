#include "Features/Streets/CSStreetRoute.h"

#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

// 기준선 스플라인을 루트로 생성하고 Tick을 비활성화
ACSStreetRoute::ACSStreetRoute()
{
	// 경로 계산은 요청 시에만 처리하므로 Tick을 사용하지 않음
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	SetRootComponent(Spline);

	// 데이터 전용 기준선이므로 충돌과 내비게이션 영향을 차단
	Spline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Spline->SetCanEverAffectNavigation(false);
}

// -----------------------------------------------------------------------
// 기준선 조회
// -----------------------------------------------------------------------

// 월드 위치를 기준선에 투영해 기준선 위 거리와 옆 거리를 조회
bool ACSStreetRoute::GetRouteCoordinates(
	const FVector& WorldLocation,
	float& OutDistance,
	float& OutLateral) const
{
	OutDistance = 0.0f;
	OutLateral = 0.0f;

	// 점이 2개 미만이면 진행 방향을 정할 수 없으므로 실패 처리
	if (!IsValid(Spline) || Spline->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	// 가장 가까운 기준선 위치의 거리를 조회
	const float InputKey = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	OutDistance = Spline->GetDistanceAlongSplineAtSplineInputKey(InputKey);

	// 높이 차이를 제외하고 기준선 오른쪽 방향으로 떨어진 거리를 계산
	const FVector OnLine = Spline->GetLocationAtDistanceAlongSpline(
		OutDistance, ESplineCoordinateSpace::World);
	const FVector Offset = (WorldLocation - OnLine) * FVector(1.0f, 1.0f, 0.0f);
	OutLateral = FVector::DotProduct(Offset, GetFlatRightAtDistance(OutDistance));

	return true;
}

// 기준선 위 거리에서 몸 반경을 뺀 레인 최대 거리를 반환
float ACSStreetRoute::GetLaneLimitAtDistance(
	float Distance,
	float BodyRadius) const
{
	if (!IsValid(Spline))
	{
		return 0.0f;
	}

	// 점별 Scale.Y를 곱해 다리처럼 좁은 구간의 폭을 반영
	const float WidthScale = FMath::Max(Spline->GetScaleAtDistanceAlongSpline(Distance).Y, 0.0f);
	const float HalfWidth = RoadHalfWidth * WidthScale;

	return FMath::Max(HalfWidth - BodyRadius, 0.0f);
}

// 가게 입구의 기준선 위 거리를 반환
float ACSStreetRoute::GetStorefrontDistance() const
{
	// 입구 마커가 있으면 마커 위치를 기준선에 투영한 거리를 사용
	if (IsValid(StorefrontMarker))
	{
		float Distance = 0.0f;
		float Lateral = 0.0f;
		if (GetRouteCoordinates(StorefrontMarker->GetActorLocation(), Distance, Lateral))
		{
			return Distance;
		}
	}

	return StorefrontDistance;
}

// 가게 입구 거리가 지정되어 있는지 확인
bool ACSStreetRoute::HasStorefront() const
{
	return GetStorefrontDistance() >= 0.0f;
}

// 월드 위치의 옆 거리를 그 지점의 레인 최대 거리로 나눈 레인 비율을 반환
float ACSStreetRoute::GetLaneRatio(
	const FVector& WorldLocation,
	float BodyRadius) const
{
	float Distance = 0.0f;
	float Lateral = 0.0f;
	if (!GetRouteCoordinates(WorldLocation, Distance, Lateral))
	{
		return 0.0f;
	}

	// 구간마다 길 폭이 다를 수 있으므로 cm가 아니라 레인 최대 거리 대비 비율로 반환
	const float Limit = GetLaneLimitAtDistance(Distance, BodyRadius);
	if (Limit <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	return FMath::Clamp(Lateral / Limit, -1.0f, 1.0f);
}

// 현재 위치가 목적지 방향 기준으로 가게 입구 거리를 지났는지 확인
bool ACSStreetRoute::IsPastStorefront(
	const FVector& WorldLocation,
	const FVector& Destination) const
{
	const float Storefront = GetStorefrontDistance();
	if (Storefront < 0.0f)
	{
		return false;
	}

	float Distance = 0.0f;
	float Lateral = 0.0f;
	float DestinationDistance = 0.0f;
	float DestinationLateral = 0.0f;

	if (!GetRouteCoordinates(WorldLocation, Distance, Lateral) ||
		!GetRouteCoordinates(Destination, DestinationDistance, DestinationLateral))
	{
		return false;
	}

	// 목적지가 입구보다 앞이면 기준선 정방향, 뒤면 역방향으로 이동 중인 것으로 판단
	float WalkSign = 1.0f;
	if (DestinationDistance < Storefront)
	{
		WalkSign = -1.0f;
	}

	return (Distance - Storefront) * WalkSign >= 0.0f;
}

// 매장에서 나온 NPC가 레인에 합류할 지면 좌표를 계산
bool ACSStreetRoute::GetMergeLocation(
	float LaneRatio,
	float BodyRadius,
	const FVector& Destination,
	FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;

	const float Storefront = GetStorefrontDistance();
	if (Storefront < 0.0f)
	{
		return false;
	}

	float DestinationDistance = 0.0f;
	float DestinationLateral = 0.0f;
	if (!GetRouteCoordinates(Destination, DestinationDistance, DestinationLateral))
	{
		return false;
	}

	// 입구 거리에서 목적지 방향으로 MergeAheadDistance만큼 이동하되 목적지 거리를 넘지 않도록 제한
	float MergeDistance = Storefront;
	if (DestinationDistance >= Storefront)
	{
		MergeDistance = FMath::Min(Storefront + MergeAheadDistance, DestinationDistance);
	}
	else
	{
		MergeDistance = FMath::Max(Storefront - MergeAheadDistance, DestinationDistance);
	}

	// 레인 비율에 합류 지점의 레인 최대 거리를 곱해 옆 거리를 계산
	// 비율이 기준선 오른쪽 기준이라 진행 방향과 관계없이 부호를 그대로 사용
	const float Lateral =
		FMath::Clamp(LaneRatio, -1.0f, 1.0f) * GetLaneLimitAtDistance(MergeDistance, BodyRadius);

	// BuildLanePath와 같은 검색 범위로 NavMesh에 투영
	// 높이는 FCSStreetLaneRequest의 기본 QueryHalfHeight와 같은 값을 사용
	const float QueryRadius = FMath::Max(BodyRadius, 50.0f);
	const FVector QueryExtent(QueryRadius, QueryRadius, 250.0f);

	return ProjectLanePoint(MergeDistance, Lateral, QueryExtent, OutLocation);
}

// 기준선 위 거리의 수평 진행 방향 기준 오른쪽 벡터를 반환
FVector ACSStreetRoute::GetFlatRightAtDistance(float Distance) const
{
	const FVector Direction = Spline->GetDirectionAtDistanceAlongSpline(
		Distance, ESplineCoordinateSpace::World).GetSafeNormal2D();

	// 위쪽 축과 진행 방향의 외적으로 지면과 평행한 오른쪽 방향을 계산
	return FVector::CrossProduct(FVector::UpVector, Direction);
}

// 기준선 위 거리와 옆 거리로 월드 좌표를 계산
FVector ACSStreetRoute::GetLaneLocation(
	float Distance,
	float Lateral) const
{
	const FVector OnLine = Spline->GetLocationAtDistanceAlongSpline(
		Distance, ESplineCoordinateSpace::World);

	return OnLine + GetFlatRightAtDistance(Distance) * Lateral;
}

// -----------------------------------------------------------------------
// 레인 경로 생성
// -----------------------------------------------------------------------

// 출발점과 도착점 사이를 출발 레인 그대로 이동하는 경로 점을 생성
bool ACSStreetRoute::BuildLanePath(
	const FCSStreetLaneRequest& Request,
	TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();

	// 출발점과 도착점을 기준선 좌표로 변환
	// 도착점은 기준선 위 거리만 사용하고 옆 거리는 출발 레인을 유지
	float StartDistance = 0.0f;
	float StartLateral = 0.0f;
	float EndDistance = 0.0f;
	float EndLateral = 0.0f;

	if (!GetRouteCoordinates(Request.StartGround, StartDistance, StartLateral) ||
		!GetRouteCoordinates(Request.EndGround, EndDistance, EndLateral))
	{
		return false;
	}

	// 도착 거리가 출발 거리보다 작으면 기준선 역방향으로 이동
	float WalkSign = 1.0f;
	if (EndDistance < StartDistance)
	{
		WalkSign = -1.0f;
	}

	const float TravelDistance = FMath::Abs(EndDistance - StartDistance);

	OutPoints.Add(Request.StartGround);

	// 기준선 위 이동 거리가 샘플 간격보다 짧으면 출발점과 도착점만 연결
	if (TravelDistance < SampleSpacing)
	{
		OutPoints.Add(Request.EndGround);
		return true;
	}

	// 옆 거리를 진행 방향 기준으로 변환해 역방향에서도 오른쪽이 양수가 되도록 설정
	const float StartLaneWalk = StartLateral * WalkSign;

	// 출발 위치의 옆 거리를 레인 비율로 변환해 이동 중 유지할 레인을 결정
	// 구간 폭에 비율을 곱하므로 좁은 구간에서는 레인이 모였다가 다시 퍼짐
	const float StartLimit = GetLaneLimitAtDistance(StartDistance, Request.BodyRadius);
	float CruiseRatio = 0.0f;
	if (StartLimit > KINDA_SMALL_NUMBER)
	{
		CruiseRatio = FMath::Clamp(StartLaneWalk / StartLimit, -1.0f, 1.0f);
	}

	// 몸 크기와 높이 차이를 반영해 NavMesh 투영 검색 범위를 설정
	const float QueryRadius = FMath::Max(Request.BodyRadius, 50.0f);
	const FVector QueryExtent(QueryRadius, QueryRadius, Request.QueryHalfHeight);

	const int32 SampleCount = FMath::FloorToInt(TravelDistance / SampleSpacing);
	OutPoints.Reserve(SampleCount + 2);

	int32 FailedCount = 0;

	// 출발점과 도착점 사이를 샘플 간격으로 나누어 레인 좌표를 계산
	for (int32 Index = 1; Index <= SampleCount; ++Index)
	{
		const float Walked = Index * SampleSpacing;

		// 도착점과 너무 가까운 샘플은 제외해 마지막 구간의 급격한 꺾임을 방지
		if (TravelDistance - Walked < SampleSpacing * 0.5f)
		{
			break;
		}

		const float Distance = StartDistance + Walked * WalkSign;
		const float Limit = GetLaneLimitAtDistance(Distance, Request.BodyRadius);

		// 출발 레인 비율에 구간 폭을 곱해 진행 레인을 계산
		const float CruiseLane = CruiseRatio * Limit;

		// 출발 구간에서는 출발 위치의 옆 거리에서 진행 레인으로 이동
		float StartAlpha = 1.0f;
		if (StartBlendDistance > KINDA_SMALL_NUMBER)
		{
			StartAlpha = FMath::SmoothStep(0.0f, StartBlendDistance, Walked);
		}

		float Lane = FMath::Lerp(StartLaneWalk, CruiseLane, StartAlpha);

		// 구간 폭을 벗어나지 않도록 레인을 제한
		Lane = FMath::Clamp(Lane, -Limit, Limit);

		// 진행 방향 기준 레인을 기준선 오른쪽 기준 옆 거리로 되돌려 투영
		FVector Projected;
		if (ProjectLanePoint(Distance, Lane * WalkSign, QueryExtent, Projected))
		{
			OutPoints.Add(Projected);
		}
		else
		{
			++FailedCount;
		}
	}

	// 투영에 실패한 샘플은 제외하고 경고를 남김
	if (FailedCount > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CSStreetRoute] %s 경로 점 %d개가 NavMesh 밖이라 제외했습니다. RoadHalfWidth와 기준선 위치, NavMesh 범위를 확인하세요."),
			*GetName(),
			FailedCount);
	}

	// 도착 거리에서 진행 레인을 유지한 위치를 마지막 점으로 사용
	// 투영에 실패하면 도착 영역에서 뽑은 위치를 사용
	const float EndLimit = GetLaneLimitAtDistance(EndDistance, Request.BodyRadius);
	const float EndLane = CruiseRatio * EndLimit;

	FVector EndPoint;
	if (!ProjectLanePoint(EndDistance, EndLane * WalkSign, QueryExtent, EndPoint))
	{
		EndPoint = Request.EndGround;
	}

	OutPoints.Add(EndPoint);
	return true;
}

// 레인 좌표를 NavMesh에 투영
bool ACSStreetRoute::ProjectLanePoint(
	float Distance,
	float Lateral,
	const FVector& QueryExtent,
	FVector& OutLocation) const
{
	const UNavigationSystemV1* NavSys =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());

	// 내비게이션 시스템이 없으면 투영 없이 기준선 높이의 좌표를 사용
	if (NavSys == nullptr)
	{
		OutLocation = GetLaneLocation(Distance, Lateral);
		return true;
	}

	// 옆 거리를 원래 값, 절반, 0 순서로 줄이며 투영을 재시도
	const float LateralScales[] = { 1.0f, 0.5f, 0.0f };
	for (const float Scale : LateralScales)
	{
		const FVector Candidate = GetLaneLocation(Distance, Lateral * Scale);

		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Candidate, Projected, QueryExtent))
		{
			OutLocation = Projected.Location;
			return true;
		}
	}

	return false;
}
