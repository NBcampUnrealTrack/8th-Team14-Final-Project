#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "CSPedestrianSpawner.generated.h"

class ACSPedestrianCharacter;
class ACSStoreGameState;
class ACSStreetRoute;
class UCSCustomerArchetypeDataAsset;

// 활성 행인 한 명의 경로와 이동 진행 상태
// 이동 판단은 스포너에서 처리하므로 관련 데이터를 함께 보관
USTRUCT()
struct FCSActivePedestrian
{
	GENERATED_BODY()

	// 현재 이동 중인 행인 캐릭터
	UPROPERTY()
	TObjectPtr<ACSPedestrianCharacter> Pedestrian = nullptr;

	// 이동할 지면 경로 점 목록
	// 첫 점은 출발 위치이며, 마지막 점은 퇴장 위치
	TArray<FVector> PathPoints;

	// 다음으로 이동할 경로 점의 인덱스
	int32 NextPointIndex = 1;

	// 마지막으로 최소 이동 거리를 충족한 기준 위치
	FVector LastProgressLocation = FVector::ZeroVector;

	// 기준 위치에서 최소 이동 거리를 충족하지 못한 누적 시간
	float StuckElapsed = 0.0f;
};

// 두 끝점 영역 사이를 이동하는 배경용 행인을 관리
// 스폰 시 경로를 계산하고 매 프레임 활성 행인에게 이동 입력을 전달
// 퇴장한 행인은 풀에 보관해 재사용하며, 손님 행동과 통계에는 참여하지 않음
UCLASS()
class CONVENIENCESTORE_API ACSPedestrianSpawner : public AActor
{
	GENERATED_BODY()

public:
	// 이동 입력용 Tick을 설정하되 시작 시에는 비활성화
	ACSPedestrianSpawner();

	// 활성 행인의 경로 진행과 끼임 상태를 확인하고 이동 입력을 전달
	virtual void Tick(float DeltaSeconds) override;

protected:
	// 난수 스트림과 이벤트 구독을 초기화하고 스폰 타이머를 시작
	virtual void BeginPlay() override;

	// 타이머와 이벤트 구독을 해제하고 관리 중인 행인 목록을 정리
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 생성 조건을 확인하고 종족, 위치, 경로를 정해 행인 한 명을 활성화
	UFUNCTION()
	void TrySpawnPedestrian();

	// 마감 정산 요청 시 현재 활성 행인을 모두 풀로 반납
	UFUNCTION()
	void HandleDailySummaryRequested();

	// -----------------------------------------------------------------------
	// 생성 설정
	// -----------------------------------------------------------------------

	// 생성할 행인 캐릭터 클래스
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn")
	TSubclassOf<ACSPedestrianCharacter> PedestrianClass;

	// 외형, 크기, 등장 가중치를 읽을 종족 데이터 목록
	// Visual, CharacterScale, SpawnWeightBase를 사용
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn")
	TArray<TObjectPtr<UCSCustomerArchetypeDataAsset>> Archetypes;

	// 이동 경로의 한쪽 끝 영역
	// 박스가 루트인 액터는 박스 회전을 반영해 위치를 추출
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn")
	TObjectPtr<AActor> EndpointA;

	// 이동 경로의 반대쪽 끝 영역
	// 스폰마다 A→B 또는 B→A 방향을 무작위로 선택
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn")
	TObjectPtr<AActor> EndpointB;

	// 행인이 따라 걸을 길 기준선
	// 지정하면 기준선을 평행 이동한 레인 경로를 사용하고, 비어 있으면 NavMesh 최단 경로를 사용
	// 두 끝점 영역은 기준선 양 끝 근처에 배치
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn")
	TObjectPtr<ACSStreetRoute> Route;

	// 행인 한 명의 스폰을 시도하는 시간 간격
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn",
		meta = (ClampMin = "0.1"))
	float SpawnInterval = 7.0f;

	// 이 스포너에서 동시에 활성화할 최대 행인 수
	// 다른 스포너의 행인은 포함하지 않음
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn",
		meta = (ClampMin = "0"))
	int32 MaxAlivePedestrians = 3;

	// true이면 게임 상태의 손님 생성 허용 여부를 확인해 스폰을 제한
	// false이면 손님 생성 허용 여부와 관계없이 스폰을 시도
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Spawn")
	bool bSpawnOnlyWhenCustomersCanSpawn = false;

	// -----------------------------------------------------------------------
	// 풀 설정
	// -----------------------------------------------------------------------

	// 시작 시 미리 생성해 풀에 보관할 행인 수
	// MaxAlivePedestrians보다 크면 사용되지 않는 비활성 액터가 남음
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Performance",
		meta = (ClampMin = "0"))
	int32 PrewarmPoolSize = 3;

	// -----------------------------------------------------------------------
	// 이동 설정
	// -----------------------------------------------------------------------

	// 걷기 속도 랜덤 배율의 최소값
	// 손님과 같은 방식으로 PedestrianClass 기본 MaxWalkSpeed에 곱함
	// 손님 스포너 기준값: 0.85 (CSCustomerSpawner.cpp)
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Movement",
		meta = (ClampMin = "0.0"))
	float WalkSpeedMultiplierMin = 0.85f;

	// 걷기 속도 랜덤 배율의 최대값
	// 최소값보다 작으면 최소값을 사용
	// 손님 스포너 기준값: 1.15 (CSCustomerSpawner.cpp)
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Movement",
		meta = (ClampMin = "0.0"))
	float WalkSpeedMultiplierMax = 1.15f;

	// 경로 진행 방향으로 남은 거리가 이 값 이하이면 다음 점으로 이동
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Movement",
		meta = (ClampMin = "0.0"))
	float PassLookahead = 40.0f;

	// 코너 보정의 기준이 되는 내비게이션 에이전트 반경
	// 프로젝트에서 사용하는 NavMesh 에이전트 설정과 맞춰 지정
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Movement",
		meta = (ClampMin = "0.0"))
	float NavAgentRadius = 75.0f;

	// 몸 반경이 기준 반경보다 큰 행인의 코너 경로 점을 보정할지 여부
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Movement")
	bool bPushCornersForLargeBodies = true;

	// 최소 이동 거리를 충족하지 못한 상태가 지속되면 반납할 시간
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Movement",
		meta = (ClampMin = "0.5"))
	float StuckTimeoutSeconds = 4.0f;

	// 이동이 진행된 것으로 판단할 최소 수평 거리
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Movement",
		meta = (ClampMin = "1.0"))
	float StuckMinMoveDistance = 20.0f;

	// -----------------------------------------------------------------------
	// 영업 흐름
	// -----------------------------------------------------------------------

	// 시작 시 마감 정산 이벤트를 구독해 활성 행인을 모두 반납할지 여부
	UPROPERTY(EditAnywhere, Category = "Pedestrian|Flow")
	bool bReleaseAllOnDailySummary = true;

private:
	// 현재 이동 중인 행인과 이동 상태 목록
	UPROPERTY()
	TArray<FCSActivePedestrian> ActivePedestrians;

	// 비활성 상태로 재사용을 기다리는 행인 목록
	UPROPERTY()
	TArray<TObjectPtr<ACSPedestrianCharacter>> PooledPedestrians;

	// 생성 허용 여부와 마감 정산 이벤트를 확인할 게임 상태
	UPROPERTY()
	TObjectPtr<ACSStoreGameState> CachedStoreGameState;

	// 종족, 이동 방향, 위치, 속도 선택에 사용할 행인 전용 난수 스트림
	// 초기 시드는 FMath::Rand로 설정하며 cs.Seed는 직접 읽지 않음
	FRandomStream PedestrianStream;

	// 주기적인 스폰 시도를 관리할 타이머 핸들
	FTimerHandle SpawnTimerHandle;

	// 종족별 등장 가중치에 따라 하나를 선택
	UCSCustomerArchetypeDataAsset* PickArchetypeBySpawnWeight();

	// PedestrianClass 기본 MaxWalkSpeed에 랜덤 배율을 곱해 걷기 속도를 반환
	// 풀 재사용 시 배율이 누적되지 않도록 활성 액터가 아닌 기본 객체 값을 기준으로 계산
	float RollWalkSpeed();

	// 클래스 기본 캡슐에 종족 스케일을 적용한 반경과 반높이를 반환
	bool GetBodySize(
		float CharacterScale,
		float& OutRadius,
		float& OutHalfHeight) const;

	// 손님 스포너와 같이 영역 내부에서 후보를 1번 뽑고 NavMesh에 투영한 지면 좌표를 반환
	// 영역이 유효하지 않으면 FallbackLocation, 크기 없는 지점 액터면 액터 위치,
	// NavMesh 투영에 실패하면 영역 중심을 반환해 스폰을 건너뛰지 않음
	FVector PickGroundPointInArea(
		const AActor* AreaActor,
		const FVector& FallbackLocation,
		float BodyRadius,
		float BodyHalfHeight);

	// 출발지에서 퇴장 위치까지 NavMesh 경로 점을 계산
	// 경로를 찾지 못하면 직선 경로, 부분 경로면 끝에 퇴장 위치를 이어 붙여 항상 2개 이상의 점을 채움
	void BuildPathPoints(
		const FVector& StartGround,
		const FVector& EndGround,
		TArray<FVector>& OutPoints) const;

	// 몸 반경이 기준 반경보다 크면 중간 코너 점을 바깥 방향으로 보정
	void PushPathCornersOutward(
		TArray<FVector>& Points,
		float BodyRadius) const;

	// 경로 진행과 끼임을 검사한 뒤 이동 입력을 전달
	// 경로 종료 또는 끼임으로 반납해야 하면 false를 반환
	bool UpdatePedestrian(
		FCSActivePedestrian& Entry,
		float DeltaSeconds);

	// 경로 구간 방향을 기준으로 목표 점에 도달하거나 통과했는지 반환
	bool HasPassedPoint(
		const FVector& From,
		const FVector& To,
		const FVector& Location) const;

	// 풀에서 유효한 행인을 꺼내고, 없으면 새로 생성
	ACSPedestrianCharacter* AcquirePedestrian(
		const FTransform& SpawnTransform);

	// 풀을 거치지 않고 새 행인 액터를 생성하고 이동 Tick 순서를 지정
	ACSPedestrianCharacter* SpawnNewPedestrian(
		const FTransform& SpawnTransform);

	// 지정한 활성 항목을 제거하고 행인을 비활성화해 풀로 반납
	void ReleaseAt(int32 ActiveIndex);

	// 활성 행인을 모두 반납하고 스포너 Tick을 중단
	void ReleaseAll();

	// 시작 시 PrewarmPoolSize만큼 새 행인을 생성해 비활성 상태로 풀에 보관
	void PrewarmPool();
};
