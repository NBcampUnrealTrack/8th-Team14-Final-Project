#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSCustomerBasketActor.generated.h"

class UArrowComponent;
class USceneComponent;
class UStaticMeshComponent;
class ACSProductPickupActor;
class ACSCustomerCharacter;
struct FCSSelectProduct;
struct FHitResult;

// 고객이 선택한 상품을 시각적으로 담아 보여 주는 장바구니(슬라임) 액터입니다.
// 손님에게 Attach하지 않고 월드에 따로 서서, 손님이 걸어간 발자국을 NavMesh 위에서 따라다닙니다.
UCLASS()
class CONVENIENCESTORE_API ACSCustomerBasketActor : public AActor
{
	GENERATED_BODY()

public:
	ACSCustomerBasketActor();

	virtual void Tick(float DeltaSeconds) override;
	// 에디터에서 메시를 교체하면 즉시 반경을 다시 계산한다
	virtual void OnConstruction(const FTransform& Transform) override;
	// 런타임 보장. 컴포넌트가 등록된 뒤 BeginPlay 전에 반경을 확정한다
	virtual void PostInitializeComponents() override;

	// 구매 목록에 추가된 상품을 장바구니의 다음 빈 표시 슬롯에 생성합니다.
	bool AddDisplayProduct(const FCSSelectProduct& SelectedProduct);
	// 계산, 반품, 이탈 시 표시용 상품을 모두 제거합니다.
	void ClearDisplayProducts();
	// 계산 중에는 장바구니와 표시 상품을 함께 숨기거나 다시 보여 줍니다.
	// 숨기면 추적 틱을 끄고, 다시 보이면 손님 옆 Park 자리로 순간이동한 뒤 추적을 재개
	void SetBasketDisplayVisible(bool bShouldBeVisible);

	// 손님을 따라다니기 시작한다. Park 자리로 순간이동한 뒤 틱을 켠다
	void StartFollowing(ACSCustomerCharacter* InCustomer);
	// 추적을 멈추고 틱을 끈다
	void StopFollowing();

protected:
	// BasketSlot_00, BasketSlot_01 형식의 Arrow Component를 표시 위치로 사용합니다.
	void CollectDisplaySlots();
	bool TryGetDisplaySlotIndex(const UArrowComponent* SlotMarker, int32& OutSlotIndex) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BasketMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UArrowComponent>> DisplaySlots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACSProductPickupActor>> DisplayProducts;

	// -----------------------------------------------------------------------
	// 기본 추적 설정
	// ---------------------------------------------------------------------

	// 충돌 검사에 사용하는 가상 구체의 반경
	// 배정된 메시 바운드에서 자동으로 산출되므로 직접 편집할 수 없다 (표시 전용)
	UPROPERTY(VisibleAnywhere, Transient, Category = "Basket|Follow")
	float BasketRadius = 50.0f;

	// 자동 산출값의 상한. NavMesh AgentRadius(75)보다 작아야 진열대 관통이 NavMesh 제한으로 막힘
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "1.0"))
	float MaxBasketRadius = 74.0f;

	// 메시를 읽지 못했을 때 사용할 값
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "1.0"))
	float FallbackBasketRadius = 50.0f;

	// 이동 중 손님과 장바구니 가장자리 사이의 간격
	// 두 객체의 반경과 합산해 발자국 경로에서 뒤따를 거리를 계산
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "0.0"))
	float FollowGap = 20.0f;

	// 손님의 현재 최대 걷기 속도에 곱할 추적 속도 배율 (평소 따라갈 때 사용할 최고 속도)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "0.1"))
	float FollowSpeedRatio = 1.2f;

	// 추적 속도 벡터를 초당 변경할 최대 크기
	// 가속, 감속, 방향 전환이 급격하게 일어나지 않도록 제한 (낮을수록 부드럽지만 느리다)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "1.0"))
	float FollowAcceleration = 1200.0f;

	// 목표까지의 거리에 곱해 도착 감속 속도를 계산하는 계수 (목표까지 남은 거리를 보고 지금 달릴 속도 결정)
	// 목표에 가까워질수록 속도를 줄이는 기준 (빠르게 달리다가 바로 멈추는거보다 남은 거리에 따라 속도를 낮추면 목표에 접근할수록 부드럽게 멈추기가 가능)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "0.1"))
	float ArrivalSpeedGain = 4.0f;

	// 목표와 이 거리 이내이면 속도를 0으로 설정
	// 이 정도 가까우면 도착했다고 보는 범위 (8이면 8cm 이내면 속도를 0으로 유지)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "0.0"))
	float StopRadius = 8.0f;

	// 목표까지의 거리가 이 값을 넘으면 따라잡기 배율을 적용
	// 뒤처졌을 때 속도를 높이는 기준 (250이면 2.5m보다 멀어지면 추가 속도 배율을 적용한다는 뜻)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "0.0"))
	float CatchUpDistance = 250.0f;

	// 따라잡기 상태에서 목표 이동 속도에 추가로 곱할 배율
	// 기본 최고 속도 제한 이후 적용 (많이 뒤처졌을 때만 추가로 속도 올리기)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "1.0"))
	float CatchUpSpeedMultiplier = 1.6f;

	// 손님과의 수평 거리가 이 값을 넘으면 대기 위치로 순간이동
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "100.0"))
	float TeleportDistance = 800.0f;

	// -----------------------------------------------------------------------
	// 끼임 및 회전 설정
	// -----------------------------------------------------------------------

	// 끼임으로 판정된 누적 시간이 이 값을 넘으면 대기 위치로 순간이동
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Stuck", meta = (ClampMin = "0.1"))
	float StuckTeleportTime = 1.5f;

	// 실제 이동량을 모아 끼임 여부를 확인할 시간 구간
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Stuck", meta = (ClampMin = "0.05"))
	float StuckCheckInterval = 0.25f;

	// 판정 구간의 최소 이동량을 계산할 기준 속도
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Stuck", meta = (ClampMin = "0.0"))
	float StuckMinProgressSpeed = 40.0f;

	// 목표와 이 거리 이내이면 정상 도착으로 보고 끼임 판정에서 제외
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Stuck", meta = (ClampMin = "0.0"))
	float StuckMinTargetDistance = 60.0f;

	// 이동 방향 또는 손님이 바라보는 방향으로 회전하는 보간 속도
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow", meta = (ClampMin = "0.0"))
	float RotationInterpSpeed = 8.0f;

	// -----------------------------------------------------------------------
	// 발자국 기록 설정 (손님이 지나간 위치를 저장한 기록)
	// -----------------------------------------------------------------------

	// 손님 발 위치를 새 발자국으로 기록할 최소 수평 간격
	// 마지막으로 기록한 위치에서 손님이 수평으로 20cm 이상 이동하면 새 위치를 저장
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Trail", meta = (ClampMin = "1.0"))
	float TrailPointSpacing = 20.0f;

	// 보관할 발자국의 최대 개수
	// 추적 거리를 계산할 수 있도록 충분한 경로 길이를 확보 (가장 오래된 위치부터 삭제)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Trail", meta = (ClampMin = "4"))
	int32 MaxTrailPoints = 64;

	// -----------------------------------------------------------------------
	// 대기 위치 설정 (손님이 멈췄을 때 장바구니가 주변 어디에 대기할지)
	// -----------------------------------------------------------------------

	// 손님 수평 속도가 이 값보다 크면 이동 중으로 판정
	// 손님 속도 20 이하면 정지로 취급
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Park", meta = (ClampMin = "0.0"))
	float MovingSpeedThreshold = 20.0f;

	// 손님이 멈춘 뒤 발자국 추적에서 대기 모드로 전환할 시간
	// 짧은 멈춤에는 기존 추적을 유지하고, 실제로 멈춰 있을 때 대기하도록 기다리는 시간
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Park", meta = (ClampMin = "0.0"))
	float IdleEnterDelay = 0.25f;

	// 대기 중 손님과 장바구니 가장자리 사이 간격
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Park", meta = (ClampMin = "0.0"))
	float ParkGap = 15.0f;

	// 마지막 탐색 위치에서 손님이 이 거리보다 멀어지면 대기 위치를 재탐색
	// (현재 손님 위치와 마지막 탐색 당시 손님 위치가 50 넘어가면 재탐색)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Park", meta = (ClampMin = "0.0"))
	float ReParkDistance = 50.0f;

	// 손님 이동 여부와 별개로 대기 위치를 다시 탐색할 시간 간격 (기본으로 주기적으로 탐색할 간격)
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Park", meta = (ClampMin = "0.05"))
	float ReParkInterval = 1.0f;

	// 대기 후보를 NavMesh에 투영할 때 사용할 수평 검색 범위
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Park", meta = (ClampMin = "1.0"))
	float ParkNavProjectTolerance = 30.0f;

	// -----------------------------------------------------------------------
	// 이동 충돌 검사 설정 (이동 루트에 장애물이 있을 때 어떻게 처리할지)
	// -----------------------------------------------------------------------

	// 구체 스윕에서 검사할 장애물 오브젝트 타입
	// 손님과 플레이어 등을 통과하도록 Pawn은 제외
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Collision")
	TArray<TEnumAsByte<EObjectTypeQuery>> SweepObjectTypes;

	// -----------------------------------------------------------------------
	// 바닥 높이 설정 (계단 때문에 사용)
	// -----------------------------------------------------------------------

	// 바닥 검사에서 사용할 오브젝트 타입
	// 이동 스윕과 별도로 설정하며 Pawn은 제외
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Floor")
	TArray<TEnumAsByte<EObjectTypeQuery>> FloorTraceObjectTypes;

	// 일반 바닥 검사에서 기준 높이보다 위로 탐색할 범위
	// 스윕 구체를 바닥에서 띄우는 높이로도 사용하므로 계단 한 단(37.4cm)보다 커야 한다
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Floor", meta = (ClampMin = "0.0"))
	float FloorStepUpHeight = 45.0f;

	// 기준 바닥 높이에서 아래로 탐색할 거리
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Floor", meta = (ClampMin = "1.0"))
	float FloorTraceDownDistance = 120.0f;

	// 추적 시작과 순간이동 시 바닥을 찾기 위해 위로 탐색할 범위
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Floor", meta = (ClampMin = "0.0"))
	float FloorSnapSearchUp = 150.0f;

	// 현재 속도로 이동할 앞쪽 거리를 계산하는 미리 보기 시간
	// 앞쪽 탐색 거리는 BasketRadius를 넘지 않도록 제한
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Floor", meta = (ClampMin = "0.0"))
	float FloorProbeLeadTime = 0.15f;

	// 표시 높이가 계산된 바닥 높이를 따라가는 보간 속도
	// 0이면 보간 없이 즉시 적용
	UPROPERTY(EditDefaultsOnly, Category = "Basket|Follow|Floor", meta = (ClampMin = "0.0"))
	float FloorZInterpSpeed = 15.0f;

private:

	// 발자국을 역순으로 따라가 지정한 경로 거리만큼 뒤의 목표를 계산
	// 발자국 길이가 부족하면 false를 반환
	bool ComputeTrailTarget(const FVector& CustomerFoot, float FollowDistance, FVector& OutTarget) const;

	// 대기 목표를 반환하고 손님 이동 거리나 경과 시간에 따라 재탐색
	FVector GetParkTarget(const FVector& CustomerFoot, const FVector& CurrentLocation);

	// 손님 뒤쪽과 양옆 후보 중 이동 검사를 통과한 첫 위치를 반환
	bool FindParkLocation(FVector& OutLocation) const;

	// NavMesh 경계와 구체 스윕을 반영한 최종 이동 위치를 반환
	FVector ConstrainMove(const FVector& From, const FVector& Delta);

	// NavMesh 경계에 막히면 경계를 따라 미끄러지는 위치를 계산
	FVector NavSlideMove(const FVector& From, const FVector& To) const;

	// NavMesh 경계 충돌 여부와 충돌 위치, 수평 법선을 반환
	bool NavRaycast(const FVector& From, const FVector& To, FVector& OutHitLocation, FVector& OutHitNormal) const;

	// 구체 스윕에 막히면 벽면을 따라 한 번 미끄러지는 위치를 계산
	FVector SweepMove(const FVector& From, const FVector& To) const;

	// 구체 스윕 결과가 이동을 막는 장애물인지 반환
	bool IsSweepBlocked(const FVector& From, const FVector& To, FHitResult& OutHit) const;

	// 제한된 검색 범위에서 NavMesh로 투영 가능한 위치인지 반환
	bool IsOnNavMesh(const FVector& Location) const;

	// 두 점 사이의 직선 구간이 NavMesh 경계에 막히지 않는지 반환
	bool IsNavReachable(const FVector& From, const FVector& To, FVector* OutHit = nullptr) const;

	// 지정 위치에서 수직 트레이스로 바닥 높이를 조회
	bool TraceFloorZ(const FVector& Location, float SeedZ, float UpHeight, float& OutFloorZ) const;

	// 현재 위치와 진행 방향 앞쪽의 바닥을 확인해 보간된 표시 높이를 반환
	float UpdateFloorZ(const FVector& NewLocation, const FVector& CustomerFoot, float DeltaSeconds);

	// 시작 또는 순간이동 위치에서 바닥 높이를 보간 없이 초기화
	void SnapFloorZ(const FVector& Location, float SeedZ);

	// 바닥 기준으로 스윕 구체 중심을 띄울 높이를 반환
	float GetSweepLift() const;

	// 배정된 메시 바운드에서 BasketRadius를 산출해 가져옴
	void UpdateBasketRadiusFromMesh();

	// 최소 기록 간격을 충족하면 손님 발 위치를 추가
	void RecordTrail(const FVector& CustomerFoot);

	// 구간 단위의 실제 이동량으로 끼임 시간을 누적하거나 초기화
	void UpdateStuck(const FVector& NewLocation, float TargetDistance, float DeltaSeconds);

	// 대기 위치로 순간이동하고 추적 상태를 초기화
	void TeleportToPark(const TCHAR* Reason);

	// 발자국, 속도, 끼임, 대기 상태를 초기화하고 바닥 상태는 유지
	void ResetFollowState();

	// 손님 캡슐 중심에서 반높이를 뺀 발 위치를 반환
	FVector GetCustomerFootLocation() const;

	// 스케일이 반영된 손님 캡슐 반경을 반환
	float GetCustomerBodyRadius() const;

	// 따라갈 손님을 수명에 영향을 주지 않는 약한 참조로 보관
	TWeakObjectPtr<ACSCustomerCharacter> FollowTarget;

	// [0]이 가장 오래된 점
	TArray<FVector> TrailPoints;
	FVector FollowVelocity = FVector::ZeroVector; // 현재 추적에 사용하는 수평 속도
	float StuckTime = 0.0f; // 연속으로 끼임 판정을 받은 누적 시간
	FVector StuckWindowStartLocation = FVector::ZeroVector; // 현재 끼임 판정 구간의 시작 위치
	float StuckWindowElapsed = 0.0f; // 현재 끼임 판정 구간의 경과 시간
	float IdleTime = 0.0f; // 손님이 이동 기준 속도 이하로 머문 시간

	bool bParkTargetCached = false; // 사용할 수 있는 대기 목표 캐시가 있는지 여부
	FVector CachedParkTarget = FVector::ZeroVector; // 마지막으로 선택한 대기 목표 위치
	FVector ParkSearchCustomerFoot = FVector::ZeroVector;
	float ParkSearchTime = 0.0f;// 마지막으로 대기 위치를 탐색한 월드 시간

	bool bFloorZValid = false; // 바닥 높이가 초기화되어 있는지 여부
	float FloorZ = 0.0f; // 트레이스와 앞쪽 미리 보기를 반영한 기준 바닥 높이
	float VisualZ = 0.0f;// 기준 바닥 높이를 보간해 따라가는 실제 표시 높이
	FVector LastFloorTraceLocation = FVector::ZeroVector;// 마지막으로 바닥 트레이스를 수행한 위치

	bool bBasketDisplayVisible = true;

private:
	// -----------------------------------------------------------------------
	// 알고리즘 상수 (에디터 노출 X — 로직이 이 값을 전제로 작성되어 있음)
	// -----------------------------------------------------------------------

	// 대기 자리 후보 순서: 손님 방향 기준 뒤 → 뒤-오른쪽 / 뒤-왼쪽 → 오른쪽 / 왼쪽
	static constexpr float ParkCandidateYaws[] = { 180.0f, 135.0f, -135.0f, 90.0f, -90.0f };

	// 이 값보다 법선 Z가 크면 바닥처럼 걸어갈 수 있는 면으로 보고 스윕에서 무시
	// CharacterMovement의 WalkableFloorZ(기본 0.71)와 같은 개념
	static constexpr float WalkableNormalZ = 0.7f;

	// NavMesh 경계에 부딪혔을 때 경계에서 떨어뜨려 둘 거리
	// 경계에 딱 붙이면 다음 프레임 판정이 부동소수 오차로 밖이 되어 낀다
	static constexpr float NavHitBackOff = 2.0f;

	// NavMesh 경계에 막혔을 때 경계를 따라 추가로 미끄러질 최대 횟수.
	// 1이면 직선 벽 하나, 2면 모서리에서 경계 두 개까지 처리한다.
	static constexpr int32 MaxNavSlideIterations = 2;

	// NavMesh 투영 질의의 수직 검색 범위 (층과 계단 높이차를 흡수)
	static constexpr float NavProjectHeight = 200.0f;

	// NavMesh 위에 있는지 판단할 때의 수평 허용 오차
	static constexpr float OnNavMeshQueryTolerance = 10.0f;
};
